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
                state.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                state.access = VK_ACCESS_2_SHADER_READ_BIT;
                state.stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                break;
            case ResourceUsage::RaytraceSampled:
                state.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                state.access = VK_ACCESS_2_SHADER_READ_BIT;
                state.stage = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
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
        DestroyResources(true);
    }

    void RenderGraph::Compile()
    {
        // 1. Physical Allocation & Usage Refinement
        for (auto& res : m_Resources)
        {
            if (res.image.handle == VK_NULL_HANDLE)
            {
                bool isDepth = VulkanUtils::IsDepthFormat(res.desc.format);
                VkImageUsageFlags finalUsage = res.desc.usage;
                
                // CRITICAL: Ensure Depth formats never have COLOR_ATTACHMENT_BIT
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

        // 2. Lock Pass Signatures (Used for Pipeline Creation)
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
                if (req.handle == INVALID_RESOURCE) continue;
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
                VkViewport vp{ 0.0f, 0.0f, (float)m_Width, (float)m_Height, 0.0f, 1.0f };
                VkRect2D sc{ {0, 0}, {m_Width, m_Height} };
                vkCmdSetViewport(cmd, 0, 1, &vp);
                vkCmdSetScissor(cmd, 0, 1, &sc);

                bool active = BeginDynamicRendering(cmd, pass);
                RenderGraphRegistry reg{ *this, pass };
                pass.executeFunc(reg, cmd);
                if (active) vkCmdEndRendering(cmd);
            }
            EndPassDebugLabel(cmd);
        }

        UpdatePersistentResources(cmd);
        return VK_NULL_HANDLE;
    }

    void RenderGraph::Reset() { m_PassStack.clear(); }

    void RenderGraph::DestroyResources(bool all)
    {
        for (auto& res : m_Resources)
        {
            if (res.image.handle != VK_NULL_HANDLE && !res.image.is_external)
                ResourceManager::Get().DestroyGraphImage(res.image);
        }
        if (all) { m_Resources.clear(); m_ResourceMap.clear(); m_HistoryResources.clear(); }
    }

    RGResourceHandle RenderGraph::PassBuilder::Read(const std::string& name)
    {
        RGResourceHandle h = graph.GetResourceHandle(name);
        if (h != INVALID_RESOURCE) pass.inputs.push_back({ h, ResourceUsage::GraphicsSampled });
        return h;
    }

    RGResourceHandle RenderGraph::PassBuilder::ReadCompute(const std::string& name)
    {
        RGResourceHandle h = graph.GetResourceHandle(name);
        if (h != INVALID_RESOURCE) pass.inputs.push_back({ h, ResourceUsage::ComputeSampled });
        return h;
    }

    RGResourceHandle RenderGraph::PassBuilder::ReadHistory(const std::string& name) { return graph.GetResourceHandle(name); }

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
        
        // Correct Usage if format changed to Depth
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

    ResourceHandleProxy& ResourceHandleProxy::Clear(const VkClearColorValue& c) { for (auto& out : pass.outputs) if (out.handle == handle) out.clearValue.color = c; return *this; }
    ResourceHandleProxy& ResourceHandleProxy::ClearDepthStencil(float d, uint32_t s) { for (auto& out : pass.outputs) if (out.handle == handle) out.clearValue.depthStencil = { d, s }; return *this; }
    ResourceHandleProxy& ResourceHandleProxy::Persistent() { return *this; }
    ResourceHandleProxy& ResourceHandleProxy::SaveAsHistory(const std::string& n) { return *this; }

    RGResourceHandle RenderGraph::GetResourceHandle(const std::string& name) { return m_ResourceMap.count(name) ? m_ResourceMap[name] : INVALID_RESOURCE; }
    void RenderGraph::DrawPerformanceStatistics() {}
    std::string RenderGraph::ExportToMermaid() const { return ""; }
    bool RenderGraph::ContainsImage(const std::string& name) { return m_ResourceMap.count(name); }
    const GraphImage& RenderGraph::GetImage(const std::string& name) const { static GraphImage empty{}; return m_ResourceMap.count(name) ? m_Resources[m_ResourceMap.at(name)].image : empty; }

    void RenderGraph::BeginPassDebugLabel(VkCommandBuffer cmd, const RenderPass& pass)
    {
        VkDebugUtilsLabelEXT l{ VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, nullptr, pass.name.c_str(), {0.8f, 0.8f, 0.1f, 1.0f} };
        if (vkCmdBeginDebugUtilsLabelEXT) vkCmdBeginDebugUtilsLabelEXT(cmd, &l);
    }

    void RenderGraph::EndPassDebugLabel(VkCommandBuffer cmd) { if (vkCmdEndDebugUtilsLabelEXT) vkCmdEndDebugUtilsLabelEXT(cmd); }

    bool RenderGraph::BeginDynamicRendering(VkCommandBuffer cmd, const RenderPass& pass)
    {
        if (pass.colorFormats.empty() && pass.depthFormat == VK_FORMAT_UNDEFINED) return false;
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
                    hasDepth = true; break;
                }
            }
        }
        VkRenderingInfo info{ VK_STRUCTURE_TYPE_RENDERING_INFO, nullptr, 0, {{0,0}, {m_Width, m_Height}}, 1, 0, (uint32_t)colorAtts.size(), colorAtts.data(), hasDepth ? &depthAtt : nullptr, nullptr };
        vkCmdBeginRendering(cmd, &info);
        return true;
    }

    void RenderGraph::UpdatePersistentResources(VkCommandBuffer cmd) 
    {
        // CRITICAL: Transition non-depth output textures to SHADER_READ_ONLY for ImGui Preview
        for (auto& res : m_Resources)
        {
            if (res.image.handle == VK_NULL_HANDLE || res.name == RS::RENDER_OUTPUT) continue;
            
            bool isDepth = VulkanUtils::IsDepthFormat(res.desc.format);
            if (!isDepth && res.currentState.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            {
                VulkanUtils::TransitionImage(cmd, res.image.handle, res.currentState.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                res.currentState.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                res.currentState.access = VK_ACCESS_2_SHADER_READ_BIT;
                res.currentState.stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            }
        }
    }
}
