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
            state.stage = (usage == ResourceUsage::ComputeSampled)
                              ? VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
                              : VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
            break;
        case ResourceUsage::StorageWrite:
        case ResourceUsage::StorageReadWrite:
            state.layout = VK_IMAGE_LAYOUT_GENERAL;
            state.access =
                VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT;
            state.stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                          VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
            break;
        case ResourceUsage::StorageRead:
            state.layout = VK_IMAGE_LAYOUT_GENERAL;
            state.access = VK_ACCESS_2_SHADER_READ_BIT;
            state.stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                          VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
            break;
        case ResourceUsage::ColorAttachment:
            state.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            state.access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            state.stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            break;
        case ResourceUsage::DepthStencilWrite:
            state.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            state.access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            state.stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            break;
        case ResourceUsage::DepthStencilRead:
            state.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            state.access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            state.stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
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
    CH_CORE_INFO("RenderGraph: Destructor started. context count: {}",
                 m_Context.GetShared().use_count());
    DestroyResources(true);
    CH_CORE_INFO("RenderGraph: Destructor finished.");
}

void RenderGraph::Compile()
{
    // 1. Initialize or Re-initialize Physical Resources
    for (auto& res : m_Resources)
    {
        res.firstPass = 0xFFFFFFFF;
        res.lastPass = 0;

        if (res.image.handle == VK_NULL_HANDLE)
        {
            bool isDepth = VulkanUtils::IsDepthFormat(res.desc.format);
            VkImageUsageFlags finalUsage = res.desc.usage;

            finalUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                          VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                          VK_IMAGE_USAGE_SAMPLED_BIT;

            if (isDepth)
            {
                finalUsage &= ~VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
                finalUsage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            }

            res.image = ResourceManager::Get().CreateGraphImage(
                res.desc.width, res.desc.height, res.desc.format, finalUsage,
                VK_IMAGE_LAYOUT_UNDEFINED, res.desc.samples, res.name);

            res.currentState.layout = VK_IMAGE_LAYOUT_UNDEFINED;
            res.currentState.access = VK_ACCESS_2_NONE;
            res.currentState.stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        }
    }

    // 2. Map Passes to Resource Lifetimes and Attachment Formats
    for (uint32_t i = 0; i < (uint32_t)m_PassStack.size(); ++i)
    {
        auto& pass = m_PassStack[i];
        pass.colorFormats.clear();
        pass.depthFormat = VK_FORMAT_UNDEFINED;

        for (auto& out : pass.outputs)
        {
            PhysicalResource& res = m_Resources[out.handle];
            if (res.firstPass == 0xFFFFFFFF) res.firstPass = i;
            res.lastPass = i;

            if (out.usage == ResourceUsage::ColorAttachment)
            {
                pass.colorFormats.push_back(res.desc.format);
            }
            else if (out.usage == ResourceUsage::DepthStencilWrite)
            {
                pass.depthFormat = res.desc.format;
            }
        }

        for (auto& in : pass.inputs)
        {
            if (in.handle != INVALID_RESOURCE)
            {
                PhysicalResource& res = m_Resources[in.handle];
                res.lastPass = i;
            }
        }
    }

    // 3. Perform parallel analysis
    BuildDependencyGraph();
}

void RenderGraph::BuildDependencyGraph()
{
    m_ParallelLayers.clear();
    uint32_t numPasses = (uint32_t)m_PassStack.size();
    if (numPasses == 0) return;

    std::vector<uint32_t> passDepths(numPasses, 0);
    std::unordered_map<RGResourceHandle, uint32_t>
        lastWriter; // Resource -> Last pass index that wrote to it

    // 1. Leveling algorithm: Assign depth to each pass based on its
    // dependencies
    for (uint32_t i = 0; i < numPasses; ++i)
    {
        auto& pass = m_PassStack[i];
        uint32_t maxDepth = 0;

        // Check dependency: If I read what someone else wrote, I must be at
        // least one level deeper
        for (auto& input : pass.inputs)
        {
            if (lastWriter.count(input.handle))
            {
                maxDepth = std::max(maxDepth,
                                    passDepths[lastWriter[input.handle]] + 1);
            }
        }

        passDepths[i] = maxDepth;

        // Track who is the last writer of this resource
        for (auto& output : pass.outputs)
        {
            lastWriter[output.handle] = i;
        }
    }

    // Group passes into parallel layers
    uint32_t totalLayers = 0;
    for (uint32_t d : passDepths) totalLayers = std::max(totalLayers, d + 1);

    m_ParallelLayers.resize(totalLayers);
    for (uint32_t i = 0; i < numPasses; ++i)
    {
        m_ParallelLayers[passDepths[i]].push_back(i);
    }
}

void RenderGraph::BuildBarriers(VkCommandBuffer cmd, RenderGraphPass& pass,
                                uint32_t passIdx)
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

            if (res.currentState.layout != target.layout ||
                (res.currentState.access & target.access) != target.access)
            {
                VkImageMemoryBarrier2 b{
                    VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
                b.srcStageMask = res.currentState.stage;
                b.srcAccessMask = res.currentState.access;
                b.dstStageMask = target.stage;
                b.dstAccessMask = target.access;
                b.oldLayout = res.currentState.layout;
                b.newLayout = target.layout;
                b.image = res.image.handle;
                b.subresourceRange = {
                    (VkImageAspectFlags)(isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                 : VK_IMAGE_ASPECT_COLOR_BIT),
                    0, 1, 0, 1};
                barriers.push_back(b);
                res.currentState = target;
            }
        }
    };
    process(pass.inputs);
    process(pass.outputs);

    if (!barriers.empty())
    {
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                             nullptr,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             (uint32_t)barriers.size(),
                             barriers.data()};
        vkCmdPipelineBarrier2(cmd, &dep);
    }
}

VkSemaphore RenderGraph::Execute(VkCommandBuffer cmd)
{
    // New Multi-threaded Ready Execution Model: Execute by parallel layers
    for (const auto& layer : m_ParallelLayers)
    {
        // 1. Global Barrier Sync: Master thread emits all barriers for this
        // parallel layer This ensures all resource transitions are done before
        // any concurrent recording starts
        for (uint32_t passIdx : layer)
        {
            BuildBarriers(cmd, m_PassStack[passIdx], passIdx);
        }

        // 2. Parallel Recording Point:
        // In a full multi-threaded implementation, the passes within this loop
        // can be dispatched to separate worker threads to record into Secondary
        // Command Buffers.
        for (uint32_t passIdx : layer)
        {
            auto& pass = m_PassStack[passIdx];
            CH_CORE_TRACE(
                "RenderGraph: Executing pass '{}' (Parallel Layer Mode)",
                pass.name);

            BeginPassDebugLabel(cmd, pass);

            if (pass.isCompute)
            {
                RenderGraphRegistry reg{*this, pass};
                pass.executeFunc(reg, cmd);
            }
            else
            {
                // [FIX] Ensure viewport/scissor are set PER PASS to match pass
                // dimensions
                VkViewport vp{0.0f, 0.0f, (float)pass.width, (float)pass.height,
                              0.0f, 1.0f};
                VkRect2D sc{{0, 0}, {pass.width, pass.height}};
                vkCmdSetViewport(cmd, 0, 1, &vp);
                vkCmdSetScissor(cmd, 0, 1, &sc);

                bool active = BeginDynamicRendering(cmd, pass);
                RenderGraphRegistry reg{*this, pass};
                pass.executeFunc(reg, cmd);
                if (active)
                {
                    vkCmdEndRendering(cmd);
                }
            }
            EndPassDebugLabel(cmd);
        }
    }

    // [OPTIMIZATION] Only update history/persistent resources.
    UpdatePersistentResources(cmd);
    return VK_NULL_HANDLE;
}

void RenderGraph::Reset()
{
    m_PassStack.clear();
}

void RenderGraph::DestroyResources(bool all)
{
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
    pass.inputs.push_back({h, ResourceUsage::GraphicsSampled});
    return h;
}

RGResourceHandle RenderGraph::PassBuilder::ReadCompute(const std::string& name)
{
    RGResourceHandle h = graph.GetResourceHandle(name);
    pass.inputs.push_back({h, ResourceUsage::ComputeSampled});
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
            PhysicalResource res{historyName};

            auto& hist = graph.m_HistoryResources[name];
            res.image = hist.image;
            res.currentState = hist.state;
            res.desc = {hist.image.width, hist.image.height, hist.image.format,
                        0};

            // [FIX] Self-perpetuating history:
            // Mark this proxy so it also saves itself back into the pool at the
            // end of the frame.
            res.historyName = name;

            graph.m_Resources.push_back(res);
            graph.m_ResourceMap[historyName] = h;
            // CH_CORE_INFO("RenderGraph: Linked history resource '{}' for name
            // '{}'", historyName, name);
        }
        else
        {
            // [FIX] Always sync image and state from the persistent history
            // record to ensure current frame's RenderGraph has the correct
            // layout/handle.
            auto& hist = graph.m_HistoryResources[name];
            graph.m_Resources[h].image = hist.image;
            graph.m_Resources[h].currentState = hist.state;
        }

        pass.inputs.push_back({h, pass.isCompute
                                      ? ResourceUsage::ComputeSampled
                                      : ResourceUsage::GraphicsSampled});
        return h;
    }

    CH_CORE_TRACE("RenderGraph: ReadHistory('{}') failed - history not found!",
                  name);
    return INVALID_RESOURCE;
}

RGResourceHandle RenderGraph::PassBuilder::ReadHistorySafe(
    const std::string& name, const std::string& fallbackName)
{
    if (graph.HasHistory(name))
    {
        return ReadHistory(name);
    }

    // Fallback to current frame's resource (e.g. black texture or raw input)
    return ReadCompute(fallbackName);
}

ResourceHandleProxy RenderGraph::PassBuilder::Write(const std::string& name,
                                                    VkFormat format)
{
    RGResourceHandle h = graph.GetResourceHandle(name);
    if (h == INVALID_RESOURCE)
    {
        h = (RGResourceHandle)graph.m_Resources.size();
        PhysicalResource res{name};
        bool isDepth = VulkanUtils::IsDepthFormat(format);
        res.desc = {
            graph.m_Width, graph.m_Height,
            format == VK_FORMAT_UNDEFINED ? VK_FORMAT_R8G8B8A8_UNORM : format,
            (VkImageUsageFlags)(isDepth
                                    ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                                    : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)};
        graph.m_Resources.push_back(res);
        graph.m_ResourceMap[name] = h;
    }

    bool isDepth = VulkanUtils::IsDepthFormat(graph.m_Resources[h].desc.format);
    pass.outputs.push_back({h, isDepth ? ResourceUsage::DepthStencilWrite
                                       : ResourceUsage::ColorAttachment});
    return ResourceHandleProxy(graph, pass, h);
}

ResourceHandleProxy RenderGraph::PassBuilder::WriteStorage(
    const std::string& name, VkFormat format)
{
    RGResourceHandle h = graph.GetResourceHandle(name);
    if (h == INVALID_RESOURCE)
    {
        h = (RGResourceHandle)graph.m_Resources.size();
        PhysicalResource res{name};
        res.desc = {
            graph.m_Width, graph.m_Height,
            format == VK_FORMAT_UNDEFINED ? VK_FORMAT_R8G8B8A8_UNORM : format,
            VK_IMAGE_USAGE_STORAGE_BIT};
        graph.m_Resources.push_back(res);
        graph.m_ResourceMap[name] = h;
    }
    pass.outputs.push_back({h, ResourceUsage::StorageWrite});
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
                out.clearValue.depthStencil = {0.0f, 0};
            }
            else if (!isDepth && out.usage == ResourceUsage::DepthStencilWrite)
            {
                out.usage = ResourceUsage::ColorAttachment;
                out.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
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
            out.clearValue.depthStencil = {d, s};
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

void RenderGraph::SetExternalResource(const std::string& name, VkImage image,
                                      VkImageView view, VkImageLayout layout,
                                      const ImageDescription& desc)
{
    RGResourceHandle handle = GetResourceHandle(name);
    if (handle == INVALID_RESOURCE)
    {
        handle = (uint32_t)m_Resources.size();
        m_Resources.emplace_back();
        m_ResourceMap[name] = handle;
    }

    auto& res = m_Resources[handle];
    res.name = name;
    res.desc = desc;
    res.desc.flags |= (RGResourceFlags)RGResourceFlagBits::External;

    res.image.handle = image;
    res.image.view = view;
    res.image.is_external = true;
    res.image.width = desc.width;
    res.image.height = desc.height;
    res.image.format = desc.format;

    // Track external state
    if (m_ExternalImageStates.count(image))
    {
        res.currentState = m_ExternalImageStates[image];
    }
    else
    {
        res.currentState.layout = layout;
        res.currentState.access = VK_ACCESS_2_NONE;
        res.currentState.stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    }
}

RGResourceHandle RenderGraph::GetResourceHandle(const std::string& name)
{
    return m_ResourceMap.count(name) ? m_ResourceMap[name] : INVALID_RESOURCE;
}

void RenderGraph::DrawPerformanceStatistics() {}

std::string RenderGraph::ExportToMermaid() const
{
    std::stringstream ss;
    ss << "graph LR\n";

    // Style definitions: Muted Pass nodes (Orange), Resource nodes (Blue)
    ss << "    classDef graphics "
          "fill:#FFCC80,stroke:#EF6C00,stroke-width:1px,color:#333\n";
    ss << "    classDef compute "
          "fill:#FFCC80,stroke:#EF6C00,stroke-width:1px,color:#333\n";
    ss << "    classDef raytrace "
          "fill:#FFCC80,stroke:#EF6C00,stroke-width:1px,color:#333\n";
    ss << "    classDef resource "
          "fill:#90CAF9,stroke:#1565C0,stroke-width:1px,color:#333\n";

    std::vector<std::string> graphicsPasses;
    std::vector<std::string> computePasses;
    std::vector<std::string> raytracePasses;
    std::unordered_set<std::string> handledResources;

    int linkIndex = 0;
    std::vector<int> readLinks;
    std::vector<int> writeLinks;

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
        else if (pass.name.find("RT") != std::string::npos ||
                 pass.name.find("Ray") != std::string::npos)
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

        ss << "    " << passNode << shape << "\"" << pass.name << shaderLabel
           << "\"" << endShape << "\n";

        // Inputs: Resource -> Pass (Read) - Green Arrow
        for (const auto& in : pass.inputs)
        {
            if (in.handle == INVALID_RESOURCE ||
                in.handle >= m_Resources.size())
                continue;

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
            readLinks.push_back(linkIndex++);
        }

        // Outputs: Pass -> Resource (Write) - Red Arrow
        for (const auto& out : pass.outputs)
        {
            if (out.handle == INVALID_RESOURCE ||
                out.handle >= m_Resources.size())
                continue;

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
            writeLinks.push_back(linkIndex++);
        }
    }

    auto addClass =
        [&](const std::vector<std::string>& nodes, const std::string& className)
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

    // Apply link styles for colors
    for (int idx : readLinks)
        ss << "    linkStyle " << idx << " stroke:#00FF00,stroke-width:2px\n";
    for (int idx : writeLinks)
        ss << "    linkStyle " << idx << " stroke:#FF0000,stroke-width:2px\n";

    return ss.str();
}

const GraphImage& RenderGraph::GetImage(const std::string& name) const
{
    if (m_ResourceMap.count(name))
    {
        return m_Resources[m_ResourceMap.at(name)].image;
    }

    static GraphImage nullImage{};
    return nullImage;
}

bool RenderGraph::ContainsImage(const std::string& name)
{
    return m_ResourceMap.count(name);
}

bool RenderGraph::HasHistory(const std::string& name) const
{
    return m_HistoryResources.count(name);
}

std::vector<std::string> RenderGraph::GetDebuggableResources() const
{
    std::vector<std::string> names;
    // Always include "Final Color" or similar if needed, but here we just
    // return graph resources
    for (const auto& res : m_Resources)
    {
        if (res.image.handle != VK_NULL_HANDLE)
        {
            names.push_back(res.name);
        }
    }
    return names;
}

void RenderGraph::BeginPassDebugLabel(VkCommandBuffer cmd,
                                      const RenderGraphPass& pass)
{
    VkDebugUtilsLabelEXT l{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
                           nullptr,
                           pass.name.c_str(),
                           {0.8f, 0.8f, 0.1f, 1.0f}};
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

bool RenderGraph::BeginDynamicRendering(VkCommandBuffer cmd,
                                        const RenderGraphPass& pass)
{
    if (pass.colorFormats.empty() && pass.depthFormat == VK_FORMAT_UNDEFINED)
    {
        return false;
    }

    // Find current pass index in the stack
    uint32_t passIdx = 0xFFFFFFFF;
    for (uint32_t i = 0; i < (uint32_t)m_PassStack.size(); ++i)
    {
        if (&m_PassStack[i] == &pass)
        {
            passIdx = i;
            break;
        }
    }

    std::vector<VkRenderingAttachmentInfo> colorAtts;
    for (auto& req : pass.outputs)
    {
        if (req.usage == ResourceUsage::ColorAttachment)
        {
            PhysicalResource& res = m_Resources[req.handle];

            // [FIX] More robust loadOp determination:
            // If the pass specifically requested a clear, and it's the first
            // WRITER, use CLEAR.
            bool hasClear = false;
            if (req.clearValue.color.float32[0] != 0.0f ||
                req.clearValue.color.float32[1] != 0.0f ||
                req.clearValue.color.float32[2] != 0.0f ||
                req.clearValue.color.float32[3] != 0.0f)
                hasClear = true;

            // For Motion specifically, we almost always want to clear if it's
            // the first time we write to it this frame.
            if (res.name == RS::Motion || res.name == RS::FinalColor ||
                res.name == RS::Albedo)
                hasClear = true;

            VkAttachmentLoadOp loadOp = (res.firstPass == passIdx || hasClear)
                                            ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                            : VK_ATTACHMENT_LOAD_OP_LOAD;

            VkRenderingAttachmentInfo a{
                VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                nullptr,
                res.image.view,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_RESOLVE_MODE_NONE,
                VK_NULL_HANDLE,
                VK_IMAGE_LAYOUT_UNDEFINED,
                loadOp,
                VK_ATTACHMENT_STORE_OP_STORE,
                req.clearValue};
            colorAtts.push_back(a);
        }
    }

    VkRenderingAttachmentInfo depthAtt{
        VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    bool hasDepth = false;
    if (pass.depthFormat != VK_FORMAT_UNDEFINED)
    {
        for (auto& req : pass.outputs)
        {
            if (req.usage == ResourceUsage::DepthStencilWrite)
            {
                PhysicalResource& res = m_Resources[req.handle];
                VkAttachmentLoadOp loadOp = (res.firstPass == passIdx)
                                                ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                                : VK_ATTACHMENT_LOAD_OP_LOAD;

                depthAtt = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                            nullptr,
                            res.image.view,
                            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                            VK_RESOLVE_MODE_NONE,
                            VK_NULL_HANDLE,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            loadOp,
                            VK_ATTACHMENT_STORE_OP_STORE,
                            req.clearValue};
                hasDepth = true;
                break;
            }
        }
    }
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO,
                         nullptr,
                         0,
                         {{0, 0}, {pass.width, pass.height}},
                         1,
                         0,
                         (uint32_t)colorAtts.size(),
                         colorAtts.data(),
                         hasDepth ? &depthAtt : nullptr,
                         nullptr};
    vkCmdBeginRendering(cmd, &info);
    return true;
}

void RenderGraph::UpdatePersistentResources(VkCommandBuffer cmd)
{
    auto FreeGraphImageLocal = [&](GraphImage& img)
    {
        if (img.handle != VK_NULL_HANDLE && !img.is_external)
        {
            ResourceManager::Get().DestroyGraphImage(img);
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
        VkImageAspectFlags aspectMask =
            isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

        // [FIX] Update History via Physical Copy (True Ping-Pong)
        if (!res.historyName.empty())
        {
            // 1. Allocate history image if it doesn't exist
            if (m_HistoryResources.find(res.historyName) ==
                m_HistoryResources.end())
            {
                VkImageUsageFlags histUsage = res.desc.usage |
                                              VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                              VK_IMAGE_USAGE_SAMPLED_BIT;
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
                    res.desc.width, res.desc.height, res.desc.format, histUsage,
                    VK_IMAGE_LAYOUT_UNDEFINED, res.desc.samples,
                    "History_" + res.historyName);

                ResourceState histState{};
                histState.layout = VK_IMAGE_LAYOUT_UNDEFINED;
                histState.access = VK_ACCESS_2_NONE;
                histState.stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;

                m_HistoryResources[res.historyName] = {historyImg, histState};
            }

            auto& histRecord = m_HistoryResources[res.historyName];
            GraphImage& dstImg = histRecord.image;

            // [FIX] Skip physical copy if source and destination are the same
            // image (History Proxy case)
            if (res.image.handle == dstImg.handle)
            {
                // Still need to ensure the layout is updated for the next frame
                res.currentState.layout =
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                res.currentState.access = VK_ACCESS_2_SHADER_READ_BIT;
                res.currentState.stage =
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

                histRecord.state = res.currentState;
                continue;
            }

            // 2. Transition Source to TRANSFER_SRC
            VkImageMemoryBarrier2 srcBarrier{
                VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            srcBarrier.srcStageMask = res.currentState.stage;
            srcBarrier.srcAccessMask = res.currentState.access;
            srcBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            srcBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            srcBarrier.oldLayout = res.currentState.layout;
            srcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            srcBarrier.image = res.image.handle;
            srcBarrier.subresourceRange = {aspectMask, 0, 1, 0, 1};

            // 3. Transition Destination to TRANSFER_DST
            VkImageMemoryBarrier2 dstBarrier{
                VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            dstBarrier.srcStageMask = histRecord.state.stage;
            dstBarrier.srcAccessMask = histRecord.state.access;
            dstBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            dstBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            dstBarrier.oldLayout = histRecord.state.layout;
            dstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            dstBarrier.image = dstImg.handle;
            dstBarrier.subresourceRange = {aspectMask, 0, 1, 0, 1};

            VkImageMemoryBarrier2 copyBarriers[] = {srcBarrier, dstBarrier};
            VkDependencyInfo copyDep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                     nullptr,
                                     0,
                                     0,
                                     nullptr,
                                     0,
                                     nullptr,
                                     2,
                                     copyBarriers};
            vkCmdPipelineBarrier2(cmd, &copyDep);

            // 4. Physical GPU Copy
            VkImageCopy copyRegion{};
            copyRegion.srcSubresource = {aspectMask, 0, 0, 1};
            copyRegion.dstSubresource = {aspectMask, 0, 0, 1};
            copyRegion.extent = {res.desc.width, res.desc.height, 1};
            vkCmdCopyImage(cmd, res.image.handle,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstImg.handle,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &copyRegion);

            // 5. Restore layouts with Depth-specific intelligence
            VkImageMemoryBarrier2 postSrcBarrier = srcBarrier;
            postSrcBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            postSrcBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            postSrcBarrier.dstStageMask =
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            postSrcBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            postSrcBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            // Source depth returns to READ_ONLY_OPTIMAL for next frame setup
            postSrcBarrier.newLayout =
                isDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkImageMemoryBarrier2 postDstBarrier = dstBarrier;
            postDstBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            postDstBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            postDstBarrier.dstStageMask =
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
            postDstBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            postDstBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            // [FIX] All history resources are read via samplers in next frame,
            // so must be SHADER_READ_ONLY_OPTIMAL
            postDstBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkImageMemoryBarrier2 postBarriers[] = {postSrcBarrier,
                                                    postDstBarrier};
            VkDependencyInfo postDep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                     nullptr,
                                     0,
                                     0,
                                     nullptr,
                                     0,
                                     nullptr,
                                     2,
                                     postBarriers};
            vkCmdPipelineBarrier2(cmd, &postDep);

            res.currentState.layout = postSrcBarrier.newLayout;
            res.currentState.access = VK_ACCESS_2_SHADER_READ_BIT;
            res.currentState.stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

            histRecord.state.layout = postDstBarrier.newLayout;
            histRecord.state.access = VK_ACCESS_2_SHADER_READ_BIT;
            histRecord.state.stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        }
        else if (!res.image.is_external && !isDepth &&
                 res.currentState.layout !=
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            // Normal transition for non-history, non-external resources
            VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            b.srcStageMask = res.currentState.stage;
            b.srcAccessMask = res.currentState.access;
            b.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                             VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            b.oldLayout = res.currentState.layout;
            b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.image = res.image.handle;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

            finalBarriers.push_back(b);

            res.currentState.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            res.currentState.access = VK_ACCESS_2_SHADER_READ_BIT;
            res.currentState.stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        }
    }

    if (!finalBarriers.empty())
    {
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                             nullptr,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             (uint32_t)finalBarriers.size(),
                             finalBarriers.data()};
        vkCmdPipelineBarrier2(cmd, &dep);
    }
}
} // namespace Chimera
