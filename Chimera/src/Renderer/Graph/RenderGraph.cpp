#include "pch.h"
#include "RenderGraph.h"
#include "Renderer/Backend/VulkanContext.h"
#include "GraphicsExecutionContext.h"
#include "ComputeExecutionContext.h"
#include "RaytracingExecutionContext.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Core/Log.h"
#include "Utils/VulkanBarrier.h"
#include "Renderer/Graph/ResourceNames.h"
#include <sstream>
#include <algorithm>
#include <unordered_set>
#include <set>

namespace Chimera
{
    // --- Internal Helpers for Robust State Mapping ---
    static ResourceState GetStateFromUsage(ResourceUsage usage, bool isDepth)
    {
        ResourceState state{};
        switch (usage)
        {
            case ResourceUsage::GraphicsSampled:
                state.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                state.access = VK_ACCESS_2_SHADER_READ_BIT;
                state.stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                break;
            case ResourceUsage::ComputeSampled:
            case ResourceUsage::RaytraceSampled:
                // Prefer SHADER_READ_ONLY for sampled resources in all stages. 
                // GENERAL is only required for Storage (Read/Write) access.
                state.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                state.access = VK_ACCESS_2_SHADER_READ_BIT;
                state.stage = (usage == ResourceUsage::ComputeSampled) ? VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
                break;
            case ResourceUsage::StorageWrite:
            case ResourceUsage::StorageReadWrite:
                state.layout = VK_IMAGE_LAYOUT_GENERAL;
                state.access = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT;
                state.stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
                break;
            case ResourceUsage::StorageRead:
                state.layout = VK_IMAGE_LAYOUT_GENERAL;
                state.access = VK_ACCESS_2_SHADER_READ_BIT;
                state.stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
                break;
            case ResourceUsage::ColorAttachment:
                state.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                state.access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                state.stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                break;
            case ResourceUsage::DepthStencilWrite:
                state.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                state.access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                state.stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                break;
            case ResourceUsage::DepthStencilRead:
                state.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
                state.access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
                state.stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                break;
            case ResourceUsage::TransferSrc:
                state.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                state.access = VK_ACCESS_2_TRANSFER_READ_BIT;
                state.stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                break;
            case ResourceUsage::TransferDst:
                state.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                state.access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                state.stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                break;
            default:
                state.layout = VK_IMAGE_LAYOUT_UNDEFINED;
                state.access = VK_ACCESS_2_NONE;
                state.stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
                break;
        }
        return state;
    }

    RenderGraph::RenderGraph(VulkanContext& context, uint32_t w, uint32_t h)
        : m_Context(context), m_Width(w), m_Height(h)
    {
    }

    RenderGraph::~RenderGraph()
    {
        CH_CORE_INFO("RenderGraph: Destructor started. context count: {}", m_Context.GetShared().use_count());
        DestroyResources(true);
        CH_CORE_INFO("RenderGraph: Destructor finished.");
    }

    void RenderGraph::Compile()
    {
        for (auto& res : m_Resources)
        {
            if (res.image.handle == VK_NULL_HANDLE)
            {
                bool isDepth = VulkanUtils::IsDepthFormat(res.desc.format);
                VkImageUsageFlags finalUsage = res.desc.usage;
                
                finalUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

                if (isDepth) 
                {
                    finalUsage &= ~VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
                    finalUsage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                }

                res.image = ResourceManager::Get().CreateGraphImage(
                    res.desc.width, res.desc.height, res.desc.format, 
                    finalUsage, VK_IMAGE_LAYOUT_UNDEFINED, 
                    res.desc.samples, res.name);
                
                res.currentState.layout = VK_IMAGE_LAYOUT_UNDEFINED;
                res.currentState.access = VK_ACCESS_2_NONE;
                res.currentState.stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            }
        }

        for (auto& pass : m_PassStack)
        {
            pass.colorFormats.clear();
            pass.depthFormat = VK_FORMAT_UNDEFINED;
            for (auto& out : pass.outputs)
            {
                if (out.usage == ResourceUsage::ColorAttachment) 
                {
                    pass.colorFormats.push_back(m_Resources[out.handle].desc.format);
                }
                else if (out.usage == ResourceUsage::DepthStencilWrite) 
                {
                    pass.depthFormat = m_Resources[out.handle].desc.format;
                }
            }
        }
    }

    void RenderGraph::BuildBarriers(VkCommandBuffer cmd, RenderPass& pass, uint32_t passIdx)
    {
        std::vector<VkImageMemoryBarrier2> barriers;
        auto process = [&](std::vector<ResourceRequest>& reqs) 
        {
            for (auto& req : reqs)
            {
                if (req.handle == INVALID_RESOURCE)
                {
                    continue;
                }
                PhysicalResource& res = m_Resources[req.handle];
                bool isDepth = VulkanUtils::IsDepthFormat(res.desc.format);
                ResourceState target = GetStateFromUsage(req.usage, isDepth);

                if (res.currentState.layout != target.layout || (res.currentState.access & target.access) != target.access)
                {
                    VkImageMemoryBarrier2 b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
                    b.srcStageMask = res.currentState.stage;
                    b.srcAccessMask = res.currentState.access;
                    b.dstStageMask = target.stage;
                    b.dstAccessMask = target.access;
                    b.oldLayout = res.currentState.layout;
                    b.newLayout = target.layout;
                    b.image = res.image.handle;
                    b.subresourceRange = { (VkImageAspectFlags)(isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, 1, 0, 1 };
                    barriers.push_back(b);
                    res.currentState = target;
                }
            }
        };
        process(pass.inputs);
        process(pass.outputs);

        if (!barriers.empty())
        {
            VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 0, nullptr, 0, nullptr, (uint32_t)barriers.size(), barriers.data() };
            vkCmdPipelineBarrier2(cmd, &dep);
        }
    }

    VkSemaphore RenderGraph::Execute(VkCommandBuffer cmd)
    {
        for (uint32_t i = 0; i < (uint32_t)m_PassStack.size(); ++i)
        {
            auto& pass = m_PassStack[i];
            BeginPassDebugLabel(cmd, pass);
            BuildBarriers(cmd, pass, i);

            if (pass.isCompute)
            {
                ComputeExecutionContext ctx(*this, pass, cmd);
                pass.executeFunc(reinterpret_cast<RenderGraphRegistry&>(ctx), cmd);
            }
            else
            {
                // [FIX] Ensure viewport/scissor are set PER PASS to match RenderGraph dimensions
                VkViewport vp{ 0.0f, 0.0f, (float)m_Width, (float)m_Height, 0.0f, 1.0f };
                VkRect2D sc{ {0, 0}, {m_Width, m_Height} };
                vkCmdSetViewport(cmd, 0, 1, &vp);
                vkCmdSetScissor(cmd, 0, 1, &sc);

                bool active = BeginDynamicRendering(cmd, pass);
                RenderGraphRegistry reg{ *this, pass };
                pass.executeFunc(reg, cmd);
                if (active)
                {
                    vkCmdEndRendering(cmd);
                }
            }
            EndPassDebugLabel(cmd);
        }

        // [OPTIMIZATION] Only update history/persistent resources. 
        // Do NOT force transition every single resource to READ_ONLY here,
        // as it might break swapchain presentation in simple Forward paths.
        UpdatePersistentResources(cmd);
        return VK_NULL_HANDLE;
    }

    void RenderGraph::Reset() 
    { 
        m_PassStack.clear(); 
    }

    void RenderGraph::DestroyResources(bool all)
    {
        if (!ResourceManager::HasInstance())
        {
            return;
        }

        std::set<VkImage> destroyedImages;

        auto FreeGraphImageLocal = [&](GraphImage& img) 
        {
            if (img.handle != VK_NULL_HANDLE && !img.is_external) 
            {
                if (destroyedImages.find(img.handle) == destroyedImages.end()) 
                {
                    destroyedImages.insert(img.handle);
                    ResourceManager::Get().DestroyGraphImage(img);
                }
                
                img.handle = VK_NULL_HANDLE;
                img.view = VK_NULL_HANDLE;
                img.debug_view = VK_NULL_HANDLE;
                img.allocation = VK_NULL_HANDLE;
            }
        };

        for (auto& res : m_Resources) 
        {
            FreeGraphImageLocal(res.image);
        }

        if (all) 
        {
            for (auto& [name, hist] : m_HistoryResources) 
            {
                FreeGraphImageLocal(hist.image);
            }
            m_HistoryResources.clear();

            for (auto& pooled : m_ImagePool) 
            {
                FreeGraphImageLocal(pooled.image);
            }
            m_ImagePool.clear();

            m_Resources.clear();
            m_ResourceMap.clear();
        }
    }

    RGResourceHandle RenderGraph::PassBuilder::Read(const std::string& name)
    {
        RGResourceHandle h = graph.GetResourceHandle(name);
        pass.inputs.push_back({ h, ResourceUsage::GraphicsSampled });
        return h;
    }

    RGResourceHandle RenderGraph::PassBuilder::ReadCompute(const std::string& name)
    {
        RGResourceHandle h = graph.GetResourceHandle(name);
        pass.inputs.push_back({ h, ResourceUsage::ComputeSampled });
        return h;
    }

    RGResourceHandle RenderGraph::PassBuilder::ReadHistory(const std::string& name) 
    { 
        if (graph.m_HistoryResources.count(name))
        {
            std::string historyName = "History_" + name;
            
            RGResourceHandle h = graph.GetResourceHandle(historyName);
            if (h == INVALID_RESOURCE)
            {
                h = (RGResourceHandle)graph.m_Resources.size();
                PhysicalResource res{ historyName };
                
                auto& hist = graph.m_HistoryResources[name];
                res.image = hist.image;
                res.currentState = hist.state;
                res.desc = { hist.image.width, hist.image.height, hist.image.format, 0 };
                
                graph.m_Resources.push_back(res);
                graph.m_ResourceMap[historyName] = h;
            }

            pass.inputs.push_back({ h, pass.isCompute ? ResourceUsage::ComputeSampled : ResourceUsage::GraphicsSampled });
            return h;
        }

        return INVALID_RESOURCE; 
    }

    ResourceHandleProxy RenderGraph::PassBuilder::Write(const std::string& name, VkFormat format)
    {
        RGResourceHandle h = graph.GetResourceHandle(name);
        if (h == INVALID_RESOURCE)
        {
            h = (RGResourceHandle)graph.m_Resources.size();
            PhysicalResource res{ name };
            bool isDepth = VulkanUtils::IsDepthFormat(format);
            res.desc = { graph.m_Width, graph.m_Height, format == VK_FORMAT_UNDEFINED ? VK_FORMAT_R8G8B8A8_UNORM : format, (VkImageUsageFlags)(isDepth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) };
            graph.m_Resources.push_back(res);
            graph.m_ResourceMap[name] = h;
        }
        
        bool isDepth = VulkanUtils::IsDepthFormat(graph.m_Resources[h].desc.format);
        pass.outputs.push_back({ h, isDepth ? ResourceUsage::DepthStencilWrite : ResourceUsage::ColorAttachment });
        return ResourceHandleProxy(graph, pass, h);
    }

    ResourceHandleProxy RenderGraph::PassBuilder::WriteStorage(const std::string& name, VkFormat format)
    {
        RGResourceHandle h = graph.GetResourceHandle(name);
        if (h == INVALID_RESOURCE)
        {
            h = (RGResourceHandle)graph.m_Resources.size();
            PhysicalResource res{ name };
            res.desc = { graph.m_Width, graph.m_Height, format == VK_FORMAT_UNDEFINED ? VK_FORMAT_R8G8B8A8_UNORM : format, VK_IMAGE_USAGE_STORAGE_BIT };
            graph.m_Resources.push_back(res);
            graph.m_ResourceMap[name] = h;
        }
        pass.outputs.push_back({ h, ResourceUsage::StorageWrite });
        return ResourceHandleProxy(graph, pass, h);
    }

    ResourceHandleProxy& ResourceHandleProxy::Format(VkFormat f) 
    { 
        graph.m_Resources[handle].desc.format = f; 
        graph.m_Resources[handle].image.format = f;
        
        bool isDepth = VulkanUtils::IsDepthFormat(f);
        for (auto& out : pass.outputs)
        {
            if (out.handle == handle)
            {
                if (isDepth && out.usage == ResourceUsage::ColorAttachment)
                {
                    out.usage = ResourceUsage::DepthStencilWrite;
                    out.clearValue.depthStencil = { 0.0f, 0 };
                }
                else if (!isDepth && out.usage == ResourceUsage::DepthStencilWrite)
                {
                    out.usage = ResourceUsage::ColorAttachment;
                    out.clearValue.color = { {0.0f, 0.0f, 0.0f, 1.0f} };
                }
            }
        }
        return *this; 
    }

    ResourceHandleProxy& ResourceHandleProxy::Clear(const VkClearColorValue& c) 
    { 
        for (auto& out : pass.outputs)
        {
            if (out.handle == handle)
            {
                out.clearValue.color = c;
            }
        }
        return *this; 
    }

    ResourceHandleProxy& ResourceHandleProxy::ClearDepthStencil(float d, uint32_t s) 
    { 
        for (auto& out : pass.outputs)
        {
            if (out.handle == handle)
            {
                out.clearValue.depthStencil = { d, s };
            }
        }
        return *this; 
    }

    ResourceHandleProxy& ResourceHandleProxy::Persistent() 
    { 
        return *this; 
    }

    ResourceHandleProxy& ResourceHandleProxy::SaveAsHistory(const std::string& n) 
    { 
        graph.m_Resources[handle].historyName = n;
        return *this; 
    }

    RGResourceHandle RenderGraph::GetResourceHandle(const std::string& name) 
    { 
        return m_ResourceMap.count(name) ? m_ResourceMap[name] : INVALID_RESOURCE; 
    }

    void RenderGraph::DrawPerformanceStatistics() 
    {
    }
    
    std::string RenderGraph::ExportToMermaid() const 
    {
        std::stringstream ss;
        ss << "graph LR\n";
        
        ss << "    classDef graphics fill:#2d5a27,stroke:#afff9e,stroke-width:2px,color:#fff\n";
        ss << "    classDef compute fill:#2d3e5a,stroke:#9ecaff,stroke-width:2px,color:#fff\n";
        ss << "    classDef raytrace fill:#5a2d2d,stroke:#ff9e9e,stroke-width:2px,color:#fff\n";
        ss << "    classDef resource fill:#333,stroke:#ccc,stroke-width:1px,color:#fff,stroke-dasharray: 5 5\n";

        std::vector<std::string> graphicsPasses;
        std::vector<std::string> computePasses;
        std::vector<std::string> raytracePasses;
        std::unordered_set<std::string> handledResources;

        for (const auto& pass : m_PassStack)
        {
            std::string shape = "[";
            std::string endShape = "]";
            std::string passNode = "Pass_" + pass.name;
            std::replace(passNode.begin(), passNode.end(), ' ', '_');

            if (pass.isCompute) 
            { 
                shape = "{{";
                endShape = "}}"; 
                computePasses.push_back(passNode); 
            }
            else if (pass.name.find("RT") != std::string::npos || pass.name.find("Ray") != std::string::npos) 
            { 
                shape = "((";
                endShape = "))"; 
                raytracePasses.push_back(passNode); 
            }
            else 
            {
                graphicsPasses.push_back(passNode);
            }

            std::string shaderLabel = "";
            if (!pass.shaderNames.empty())
            {
                shaderLabel = "<br/>(";
                for (size_t i = 0; i < pass.shaderNames.size(); ++i)
                {
                    shaderLabel += pass.shaderNames[i];
                    if (i < pass.shaderNames.size() - 1)
                    {
                        shaderLabel += ", ";
                    }
                }
                shaderLabel += ")";
            }

            ss << "    " << passNode << shape << "\"" << pass.name << shaderLabel << "\"" << endShape << "\n";

            for (const auto& in : pass.inputs)
            {
                std::string resName = m_Resources[in.handle].name;
                std::string resID = "Res_" + resName;
                std::replace(resID.begin(), resID.end(), ' ', '_');

                if (handledResources.find(resID) == handledResources.end())
                {
                    ss << "    " << resID << "(\"" << resName << "\")\n";
                    ss << "    class " << resID << " resource\n";
                    handledResources.insert(resID);
                }
                ss << "    " << resID << " --> " << passNode << "\n";
            }

            for (const auto& out : pass.outputs)
            {
                std::string resName = m_Resources[out.handle].name;
                std::string resID = "Res_" + resName;
                std::replace(resID.begin(), resID.end(), ' ', '_');

                if (handledResources.find(resID) == handledResources.end())
                {
                    ss << "    " << resID << "(\"" << resName << "\")\n";
                    ss << "    class " << resID << " resource\n";
                    handledResources.insert(resID);
                }
                ss << "    " << passNode << " --> " << resID << "\n";
            }
        }

        auto addClass = [&](const std::vector<std::string>& nodes, const std::string& className)
        {
            if (!nodes.empty())
            {
                ss << "    class ";
                for (size_t i = 0; i < nodes.size(); ++i)
                {
                    ss << nodes[i] << (i < nodes.size() - 1 ? "," : "");
                }
                ss << " " << className << "\n";
            }
        };

        addClass(graphicsPasses, "graphics");
        addClass(computePasses, "compute");
        addClass(raytracePasses, "raytrace");

        return ss.str();
    }

    bool RenderGraph::ContainsImage(const std::string& name) 
    { 
        return m_ResourceMap.count(name); 
    }

    bool RenderGraph::HasHistory(const std::string& name) const
    {
        return m_HistoryResources.count(name);
    }

    const GraphImage& RenderGraph::GetImage(const std::string& name) const 
    { 
        static GraphImage empty{}; 
        return m_ResourceMap.count(name) ? m_Resources[m_ResourceMap.at(name)].image : empty; 
    }

    void RenderGraph::BeginPassDebugLabel(VkCommandBuffer cmd, const RenderPass& pass)
    {
        VkDebugUtilsLabelEXT l{ VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, nullptr, pass.name.c_str(), {0.8f, 0.8f, 0.1f, 1.0f} };
        if (vkCmdBeginDebugUtilsLabelEXT)
        {
            vkCmdBeginDebugUtilsLabelEXT(cmd, &l);
        }
    }

    void RenderGraph::EndPassDebugLabel(VkCommandBuffer cmd) 
    { 
        if (vkCmdEndDebugUtilsLabelEXT)
        {
            vkCmdEndDebugUtilsLabelEXT(cmd); 
        }
    }

    bool RenderGraph::BeginDynamicRendering(VkCommandBuffer cmd, const RenderPass& pass)
    {
        if (pass.colorFormats.empty() && pass.depthFormat == VK_FORMAT_UNDEFINED)
        {
            return false;
        }
        std::vector<VkRenderingAttachmentInfo> colorAtts;
        for (auto& req : pass.outputs)
        {
            if (req.usage == ResourceUsage::ColorAttachment)
            {
                VkRenderingAttachmentInfo a{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, nullptr, m_Resources[req.handle].image.view, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_RESOLVE_MODE_NONE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, req.clearValue };
                colorAtts.push_back(a);
            }
        }
        VkRenderingAttachmentInfo depthAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        bool hasDepth = false;
        if (pass.depthFormat != VK_FORMAT_UNDEFINED)
        {
            for (auto& req : pass.outputs)
            {
                if (req.usage == ResourceUsage::DepthStencilWrite)
                {
                    depthAtt = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, nullptr, m_Resources[req.handle].image.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_RESOLVE_MODE_NONE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, req.clearValue };
                    hasDepth = true; 
                    break;
                }
            }
        }
        VkRenderingInfo info{ VK_STRUCTURE_TYPE_RENDERING_INFO, nullptr, 0, {{0,0}, {m_Width, m_Height}}, 1, 0, (uint32_t)colorAtts.size(), colorAtts.data(), hasDepth ? &depthAtt : nullptr, nullptr };
        vkCmdBeginRendering(cmd, &info);
        return true;
    }

    void RenderGraph::UpdatePersistentResources(VkCommandBuffer cmd) 
    {
        auto FreeGraphImageLocal = [&](GraphImage& img) 
        {
            if (img.handle != VK_NULL_HANDLE && !img.is_external) 
            {
                if (ResourceManager::HasInstance())
                {
                    ResourceManager::Get().DestroyGraphImage(img);
                }
            }
            img.handle = VK_NULL_HANDLE;
            img.view = VK_NULL_HANDLE;
            img.debug_view = VK_NULL_HANDLE;
            img.allocation = VK_NULL_HANDLE;
        };

        std::vector<VkImageMemoryBarrier2> finalBarriers;

        for (auto& res : m_Resources)
        {
            if (res.image.handle == VK_NULL_HANDLE)
            {
                continue;
            }
            
            bool isDepth = VulkanUtils::IsDepthFormat(res.desc.format);
            VkImageAspectFlags aspectMask = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

            // [FIX] Update History via Physical Copy (True Ping-Pong)
            if (!res.historyName.empty())
            {
                // 1. Allocate history image if it doesn't exist
                if (m_HistoryResources.find(res.historyName) == m_HistoryResources.end())
                {
                    VkImageUsageFlags histUsage = res.desc.usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                    if (!isDepth) 
                    {
                        histUsage |= VK_IMAGE_USAGE_STORAGE_BIT;
                    }
                    else
                    {
                        histUsage &= ~VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
                        histUsage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                    }

                    GraphImage historyImg = ResourceManager::Get().CreateGraphImage(
                        res.desc.width, res.desc.height, res.desc.format,
                        histUsage,
                        VK_IMAGE_LAYOUT_UNDEFINED, 
                        res.desc.samples, 
                        "History_" + res.historyName
                    );
                    
                    ResourceState histState{};
                    histState.layout = VK_IMAGE_LAYOUT_UNDEFINED;
                    histState.access = VK_ACCESS_2_NONE;
                    histState.stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;

                    m_HistoryResources[res.historyName] = { historyImg, histState };
                }

                auto& histRecord = m_HistoryResources[res.historyName];
                GraphImage& dstImg = histRecord.image;

                // 2. Transition Source to TRANSFER_SRC
                VkImageMemoryBarrier2 srcBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
                srcBarrier.srcStageMask = res.currentState.stage;
                srcBarrier.srcAccessMask = res.currentState.access;
                srcBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                srcBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                srcBarrier.oldLayout = res.currentState.layout;
                srcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                srcBarrier.image = res.image.handle;
                srcBarrier.subresourceRange = { aspectMask, 0, 1, 0, 1 };

                // 3. Transition Destination to TRANSFER_DST
                VkImageMemoryBarrier2 dstBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
                dstBarrier.srcStageMask = histRecord.state.stage;
                dstBarrier.srcAccessMask = histRecord.state.access;
                dstBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                dstBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                dstBarrier.oldLayout = histRecord.state.layout;
                dstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                dstBarrier.image = dstImg.handle;
                dstBarrier.subresourceRange = { aspectMask, 0, 1, 0, 1 };

                VkImageMemoryBarrier2 copyBarriers[] = { srcBarrier, dstBarrier };
                VkDependencyInfo copyDep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 0, nullptr, 0, nullptr, 2, copyBarriers };
                vkCmdPipelineBarrier2(cmd, &copyDep);

                // 4. Physical GPU Copy
                VkImageCopy copyRegion{};
                copyRegion.srcSubresource = { aspectMask, 0, 0, 1 };
                copyRegion.dstSubresource = { aspectMask, 0, 0, 1 };
                copyRegion.extent = { res.desc.width, res.desc.height, 1 };
                vkCmdCopyImage(cmd, res.image.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstImg.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

                // 5. Restore layouts with Depth-specific intelligence
                VkImageMemoryBarrier2 postSrcBarrier = srcBarrier;
                postSrcBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                postSrcBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                postSrcBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                postSrcBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                postSrcBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                // Source depth returns to READ_ONLY_OPTIMAL for next frame setup
                postSrcBarrier.newLayout = isDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                VkImageMemoryBarrier2 postDstBarrier = dstBarrier;
                postDstBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                postDstBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                postDstBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
                postDstBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                postDstBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                // [FIX] All history resources are read via samplers in next frame, so must be SHADER_READ_ONLY_OPTIMAL
                postDstBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                VkImageMemoryBarrier2 postBarriers[] = { postSrcBarrier, postDstBarrier };
                VkDependencyInfo postDep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 0, nullptr, 0, nullptr, 2, postBarriers };
                vkCmdPipelineBarrier2(cmd, &postDep);

                res.currentState.layout = postSrcBarrier.newLayout;
                res.currentState.access = VK_ACCESS_2_SHADER_READ_BIT;
                res.currentState.stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

                histRecord.state.layout = postDstBarrier.newLayout;
                histRecord.state.access = VK_ACCESS_2_SHADER_READ_BIT;
                histRecord.state.stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            }
            else if (!isDepth && res.currentState.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            {
                // Normal transition for non-history resources
                VkImageMemoryBarrier2 b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
                b.srcStageMask = res.currentState.stage;
                b.srcAccessMask = res.currentState.access;
                b.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                b.oldLayout = res.currentState.layout;
                b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                b.image = res.image.handle;
                b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                
                finalBarriers.push_back(b);

                res.currentState.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                res.currentState.access = VK_ACCESS_2_SHADER_READ_BIT;
                res.currentState.stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            }
        }

        if (!finalBarriers.empty())
        {
            VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 0, nullptr, 0, nullptr, (uint32_t)finalBarriers.size(), finalBarriers.data() };
            vkCmdPipelineBarrier2(cmd, &dep);
        }
    }
}
