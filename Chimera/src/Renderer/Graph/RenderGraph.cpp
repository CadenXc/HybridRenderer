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
#include <imgui.h>

namespace Chimera
{
// --- Internal Helpers for Robust State Mapping ---

static VkImageUsageFlags GetRequiredImageUsage(ResourceUsage usage)
{
    switch (usage)
    {
        case ResourceUsage::GraphicsSampled:
        case ResourceUsage::ComputeSampled:
        case ResourceUsage::RaytraceSampled:
            return VK_IMAGE_USAGE_SAMPLED_BIT;
        case ResourceUsage::StorageRead:
        case ResourceUsage::StorageWrite:
        case ResourceUsage::StorageReadWrite:
            return VK_IMAGE_USAGE_STORAGE_BIT;
        case ResourceUsage::ColorAttachment:
            return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        case ResourceUsage::DepthStencilRead:
        case ResourceUsage::DepthStencilWrite:
            return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        case ResourceUsage::TransferSrc:
            return VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        case ResourceUsage::TransferDst:
            return VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        case ResourceUsage::None:
        default:
            return 0;
    }
}

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

bool SupportsImageUsage(VkImageUsageFlags actualUsage, ResourceUsage requestUsage)
{
    if (requestUsage == ResourceUsage::None)
    {
        return true;
    }
    VkImageUsageFlags requiredUsage = GetRequiredImageUsage(requestUsage);
    return (actualUsage & requiredUsage) == requiredUsage;
}


static bool HasImageWriteAccess(VkAccessFlags2 access)
{
    constexpr VkAccessFlags2 writeMask =
        VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;

    return (access & writeMask) != 0;
}

bool RequiresImageMemoryBarrier(const ResourceState& current,
                                const ResourceState& target)
{
    const bool stateChanged = current.layout != target.layout ||
                              (current.access & target.access) != target.access;

    return stateChanged || HasImageWriteAccess(current.access) ||
           HasImageWriteAccess(target.access);
}

RenderGraph::RenderGraph(VulkanContext& context, uint32_t w, uint32_t h)
    : m_Context(&context), m_Width(w), m_Height(h)
{
}

RenderGraph::RenderGraph(uint32_t w, uint32_t h) : m_Width(w), m_Height(h) {}

RenderGraph::~RenderGraph()
{
    if (m_Context && m_TimestampQueryPool != VK_NULL_HANDLE)
    {
        vkDestroyQueryPool(m_Context->GetDevice(), m_TimestampQueryPool,
                           nullptr);
    }
    DestroyResources(true);
}

void RenderGraph::Compile()
{
    for (const auto& pass : m_PassStack)
    {
        for (const auto& input : pass.inputs)
        {
            if (input.handle == INVALID_RESOURCE ||
                input.handle >= m_Resources.size())
            {
                throw std::logic_error(
                    "RenderGraph compile error: pass '" + pass.name +
                    "' reads undeclared resource '" + input.name + "'");
            }
        }
    }
    std::unordered_map<std::string, RGResourceHandle> historyProducers;

    for (const auto& pass : m_PassStack)
    {
        for (const auto& output : pass.outputs)
        {
            const auto& resource = m_Resources[output.handle];

            if (resource.historyName.empty())
            {
                continue;
            }

            auto [existing, inserted] =
                historyProducers.emplace(resource.historyName, output.handle);

            if (!inserted && existing->second != output.handle)
            {
                const auto& previousResource = m_Resources[existing->second];

                throw std::logic_error(
                    "RenderGraph compile error: history '" +
                    resource.historyName + "' has multiple producers: '" +
                    previousResource.name + "' and '" + resource.name + "'");
            }
        }
    }

    for (auto& res : m_Resources)
    {
        res.firstPass = 0xFFFFFFFF;
        res.lastPass = 0;

        if (res.image.handle == VK_NULL_HANDLE && m_Context != nullptr)
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

    auto validateImageUsage = [&](const RenderGraphPass& pass,
                                  const ResourceRequest& request)
    {
        if (request.handle == INVALID_RESOURCE ||
            request.handle >= m_Resources.size())
        {
            return;
        }

        const PhysicalResource& resource = m_Resources[request.handle];

        // Context-free RenderGraph instances are used by unit tests and do not
        // own physical VkImages. Their logical dependency checks remain valid.
        if (resource.image.handle == VK_NULL_HANDLE)
        {
            return;
        }

        const VkImageUsageFlags requiredUsage =
            GetRequiredImageUsage(request.usage);
        const VkImageUsageFlags actualUsage = resource.image.usage;

        if (!SupportsImageUsage(actualUsage, request.usage))
        {
            std::ostringstream message;
            message << "RenderGraph compile error: pass '" << pass.name
                    << "' requests resource '" << resource.name
                    << "' with image usage 0x" << std::hex << requiredUsage
                    << ", but the physical image was created with usage 0x"
                    << actualUsage;
            throw std::logic_error(message.str());
        }
    };

    for (const auto& pass : m_PassStack)
    {
        for (const auto& input : pass.inputs)
        {
            validateImageUsage(pass, input);
        }

        for (const auto& output : pass.outputs)
        {
            validateImageUsage(pass, output);
        }
    }

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
                pass.colorFormats.push_back(res.desc.format);
            else if (out.usage == ResourceUsage::DepthStencilWrite)
                pass.depthFormat = res.desc.format;
        }

        for (auto& in : pass.inputs)
        {
            if (in.handle != INVALID_RESOURCE)
                m_Resources[in.handle].lastPass = i;
        }
    }

    BuildDependencyGraph();
}

std::vector<std::vector<uint32_t>> RenderGraph::BuildExecutionLayers(
    const std::vector<std::vector<uint32_t>>& dependencies)
{
    const uint32_t passCount = static_cast<uint32_t>(dependencies.size());

    std::vector<std::vector<uint32_t>> successors(passCount);
    std::vector<uint32_t> indegrees(passCount, 0);

    // predecessor table 转换为 successor table 和 indegree。
    for (uint32_t dependent = 0; dependent < passCount; ++dependent)
    {
        for (uint32_t predecessor : dependencies[dependent])
        {
            if (predecessor >= passCount)
            {
                throw std::logic_error(
                    "RenderGraph dependency references an invalid pass");
            }

            auto& outgoing = successors[predecessor];

            // 防止重复 edge 被重复计入 indegree。
            if (std::find(outgoing.begin(), outgoing.end(), dependent) ==
                outgoing.end())
            {
                outgoing.push_back(dependent);
                ++indegrees[dependent];
            }
        }
    }

    std::vector<uint32_t> currentLayer;

    for (uint32_t pass = 0; pass < passCount; ++pass)
    {
        if (indegrees[pass] == 0) currentLayer.push_back(pass);
    }

    std::vector<std::vector<uint32_t>> layers;
    uint32_t processedPasses = 0;

    while (!currentLayer.empty())
    {
        // 稳定输出，便于测试和调试。
        std::sort(currentLayer.begin(), currentLayer.end());

        layers.push_back(currentLayer);

        std::vector<uint32_t> nextLayer;

        for (uint32_t predecessor : currentLayer)
        {
            ++processedPasses;

            for (uint32_t dependent : successors[predecessor])
            {
                --indegrees[dependent];

                if (indegrees[dependent] == 0) nextLayer.push_back(dependent);
            }
        }

        currentLayer = std::move(nextLayer);
    }

    if (processedPasses != passCount)
    {
        throw std::logic_error("RenderGraph dependency cycle detected");
    }

    return layers;
}

void RenderGraph::BuildDependencyGraph()
{
    m_ParallelLayers.clear();
    m_PassDependencies.clear();

    uint32_t numPasses = (uint32_t)m_PassStack.size();

    if (numPasses == 0) return;

    m_PassDependencies.resize(numPasses);

    std::unordered_map<RGResourceHandle, uint32_t> lastWriter;
    std::unordered_map<RGResourceHandle, std::vector<uint32_t>>
        readersSinceLastWrite;

    auto addDependency = [&](uint32_t predecessor, uint32_t dependent)
    {
        auto& dependencies = m_PassDependencies[dependent];

        if (std::find(dependencies.begin(), dependencies.end(), predecessor) ==
            dependencies.end())
        {
            dependencies.push_back(predecessor);
        }
    };

    for (uint32_t i = 0; i < numPasses; ++i)
    {
        auto& pass = m_PassStack[i];

    // RAW：当前 pass 读取此前 writer 的结果。
        for (const auto& input : pass.inputs)
        {
            auto writer = lastWriter.find(input.handle);

            if (writer != lastWriter.end())
            {
                addDependency(writer->second, i);
            }
        }

    // WAW：当前 pass 再次写入此前 writer 写过的资源。
        for (const auto& output : pass.outputs)
        {
            auto writer = lastWriter.find(output.handle);

            if (writer != lastWriter.end())
            {
                addDependency(writer->second, i);
            }

            auto readers = readersSinceLastWrite.find(output.handle);

            if (readers != readersSinceLastWrite.end())
            {
                for (uint32_t readerIdx : readers->second)
                {
                    addDependency(readerIdx, i);
                }
            }
        }

                // 必须先完成依赖计算，再登记当前 pass。
                // 否则 read-write pass 可能错误地依赖自己。
        for (const auto& input : pass.inputs)
        {
            if (input.handle != INVALID_RESOURCE)
            {
                readersSinceLastWrite[input.handle].push_back(i);
            }
        }

        for (const auto& output : pass.outputs)
        {
            lastWriter[output.handle] = i;

                        // 新写入覆盖旧版本。此前 reader 已经成为该 writer
                        // 的依赖，不应继续约束下一代资源版本。
            readersSinceLastWrite[output.handle].clear();
        }
    }

    m_ParallelLayers = BuildExecutionLayers(m_PassDependencies);
}

void RenderGraph::BuildBarriers(VkCommandBuffer cmd, RenderGraphPass& pass,
                                uint32_t passIdx)
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

            if (RequiresImageMemoryBarrier(res.currentState, target))
            {
                VkImageMemoryBarrier2 b{
                    VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
                b.srcStageMask = res.currentState.stage;
                b.srcAccessMask = res.currentState.access;
                b.dstStageMask = target.stage;
                b.dstAccessMask = target.access;
                b.oldLayout = res.currentState.layout;
                b.newLayout = target.layout;
                b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
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

void RenderGraph::InitQueryPool()
{
    uint32_t passCount = (uint32_t)m_PassStack.size();
    if (passCount == 0) return;

    if (m_TimestampQueryPool == VK_NULL_HANDLE ||
        m_PreviousPassCount < passCount)
    {
        if (m_TimestampQueryPool != VK_NULL_HANDLE)
            vkDestroyQueryPool(m_Context->GetDevice(), m_TimestampQueryPool,
                               nullptr);

        VkQueryPoolCreateInfo poolInfo{
            VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        poolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        poolInfo.queryCount = std::max(64u, passCount * 2);
        vkCreateQueryPool(m_Context->GetDevice(), &poolInfo, nullptr,
                          &m_TimestampQueryPool);

        // [FIX] Perform an immediate Host Reset upon creation.
        // This ensures the pool is in a valid state even before the first GPU
        // command buffer executes.
        vkResetQueryPool(m_Context->GetDevice(), m_TimestampQueryPool, 0,
                         poolInfo.queryCount);

        m_PreviousPassCount = passCount;
        m_StatsReady = false;
    }
}

void RenderGraph::WriteTimestamp(VkCommandBuffer cmd, uint32_t queryIdx,
                                 VkPipelineStageFlags2 stage)
{
    if (m_TimestampQueryPool != VK_NULL_HANDLE)
        vkCmdWriteTimestamp2(cmd, stage, m_TimestampQueryPool, queryIdx);
}

void RenderGraph::FetchQueryResults()
{
    if (m_TimestampQueryPool == VK_NULL_HANDLE || m_PassStack.empty() ||
        !m_StatsReady)
        return;

    uint32_t queryCount = (uint32_t)m_LastPassNames.size() * 2;
    if (queryCount == 0) return;

    std::vector<uint64_t> results(queryCount);

    // We don't use WAIT_BIT here to avoid any chance of blocking the main
    // thread. If results aren't ready (VK_NOT_READY), we simply skip this
    // frame's update.
    VkResult res = vkGetQueryPoolResults(
        m_Context->GetDevice(), m_TimestampQueryPool, 0, queryCount,
        results.size() * sizeof(uint64_t), results.data(), sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT);

    if (res == VK_SUCCESS)
    {
        m_LatestTimings.clear();
        float period = m_Context->GetDeviceProperties().limits.timestampPeriod;
        for (uint32_t i = 0; i < (uint32_t)m_LastPassNames.size(); ++i)
        {
            uint64_t start = results[i * 2];
            uint64_t end = results[i * 2 + 1];
            float durationMs = (end > start)
                                   ? (float)(end - start) * period / 1000000.0f
                                   : 0.0f;
            m_LatestTimings.push_back({m_LastPassNames[i], durationMs});
        }
    }
}

VkSemaphore RenderGraph::Execute(VkCommandBuffer cmd)
{
    if (m_PassStack.empty())
    {
        m_LastPassNames.clear();
        m_LatestTimings.clear();
        m_StatsReady = false;
        return VK_NULL_HANDLE;
    }

    if (!m_Context)
    {
        throw std::logic_error(
            "Compile-only RenderGraph cannot execute GPU passes");
    }

    FetchQueryResults();
    InitQueryPool();

    if (m_TimestampQueryPool != VK_NULL_HANDLE)
    {
        vkCmdResetQueryPool(cmd, m_TimestampQueryPool, 0,
                            static_cast<uint32_t>(m_PassStack.size()) * 2);
    }

    m_LastPassNames.clear();
    for (const auto& pass : m_PassStack) m_LastPassNames.push_back(pass.name);

    for (const auto& layer : m_ParallelLayers)
    {
        for (uint32_t passIdx : layer)
            BuildBarriers(cmd, m_PassStack[passIdx], passIdx);

        for (uint32_t passIdx : layer)
        {
            auto& pass = m_PassStack[passIdx];
            // Start Timestamp
            WriteTimestamp(cmd, passIdx * 2,
                           VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);

            BeginPassDebugLabel(cmd, pass);

            if (pass.isCompute)
            {
                RenderGraphRegistry reg{*this, pass};
                pass.executeFunc(reg, cmd);
            }
            else
            {
                VkViewport vp{0.0f, 0.0f, (float)pass.width, (float)pass.height,
                              0.0f, 1.0f};
                VkRect2D sc{{0, 0}, {pass.width, pass.height}};
                vkCmdSetViewport(cmd, 0, 1, &vp);
                vkCmdSetScissor(cmd, 0, 1, &sc);

                bool active = BeginDynamicRendering(cmd, pass);
                RenderGraphRegistry reg{*this, pass};
                pass.executeFunc(reg, cmd);
                if (active) vkCmdEndRendering(cmd);
            }

            EndPassDebugLabel(cmd);

            // End Timestamp
            WriteTimestamp(cmd, passIdx * 2 + 1,
                           VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
        }
    }

    UpdatePersistentResources(cmd);

    // Crucially set this to true AFTER we've recorded all commands.
    // The next frame's Execute() will then try to fetch these results.
    m_StatsReady = true;

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
    RGResourceHandle handle = graph.GetResourceHandle(name);

    ResourceRequest request{handle, ResourceUsage::GraphicsSampled};

    request.name = name;

    pass.inputs.push_back(std::move(request));

    return handle;
}

RGResourceHandle RenderGraph::PassBuilder::ReadCompute(const std::string& name)
{
    RGResourceHandle handle = graph.GetResourceHandle(name);

    ResourceRequest request{handle, ResourceUsage::ComputeSampled};

    request.name = name;
    pass.inputs.push_back(std::move(request));

    return handle;
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

            res.historyName = name;

            graph.m_Resources.push_back(res);
            graph.m_ResourceMap[historyName] = h;
        }
        else
        {
            auto& hist = graph.m_HistoryResources[name];
            graph.m_Resources[h].image = hist.image;
            graph.m_Resources[h].currentState = hist.state;
        }

        pass.inputs.push_back({h, pass.isCompute
                                      ? ResourceUsage::ComputeSampled
                                      : ResourceUsage::GraphicsSampled});
        return h;
    }

    ResourceRequest request{INVALID_RESOURCE,
                            pass.isCompute ? ResourceUsage::ComputeSampled
                                           : ResourceUsage::GraphicsSampled};

    request.name = name;
    pass.inputs.push_back(std::move(request));

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

    return pass.isCompute ? ReadCompute(fallbackName) : Read(fallbackName);
}

ResourceHandleProxy& ResourceHandleProxy::AllowUsage(VkImageUsageFlags additionalUsage)
{
    graph.m_Resources[handle].desc.usage |= additionalUsage;
    return *this;
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
    graph.m_Resources[h].desc.usage |=
        isDepth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
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
    graph.m_Resources[h].desc.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    pass.outputs.push_back({h, ResourceUsage::StorageWrite});
    return ResourceHandleProxy(graph, pass, h);
}

ResourceHandleProxy RenderGraph::PassBuilder::WriteTransfer(
    const std::string& name, VkFormat format)
{
    RGResourceHandle h = graph.GetResourceHandle(name);

    if (h == INVALID_RESOURCE)
    {
        h = static_cast<RGResourceHandle>(graph.m_Resources.size());

        PhysicalResource res{name};
        res.desc = {
            graph.m_Width, graph.m_Height,
            format == VK_FORMAT_UNDEFINED ? VK_FORMAT_R8G8B8A8_UNORM : format,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT};

        graph.m_Resources.push_back(res);
        graph.m_ResourceMap[name] = h;
    }
    graph.m_Resources[h].desc.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    pass.outputs.push_back({h, ResourceUsage::TransferDst});

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
                                      VkImageView view,
                                      const ResourceState& initialState,
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
    res.image.usage = desc.usage;

    res.currentState = initialState;
}

RGResourceHandle RenderGraph::GetResourceHandle(const std::string& name)
{
    return m_ResourceMap.count(name) ? m_ResourceMap[name] : INVALID_RESOURCE;
}

void RenderGraph::DrawPerformanceStatistics()
{
    if (m_LatestTimings.empty())
    {
        ImGui::Text("No timing data available. Ensure GPU queries are active.");
        return;
    }

    float totalTime = 0.0f;
    for (const auto& timing : m_LatestTimings) totalTime += timing.durationMS;

    if (ImGui::BeginTable("PassTimings", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_Resizable))
    {
        ImGui::TableSetupColumn("Pass Name",
                                ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("GPU Time (ms)",
                                ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Ratio (%)", ImGuiTableColumnFlags_WidthFixed,
                                80.0f);
        ImGui::TableHeadersRow();

        for (const auto& timing : m_LatestTimings)
        {
            float percentage = (totalTime > 0.0f)
                                   ? (timing.durationMS / totalTime) * 100.0f
                                   : 0.0f;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", timing.name.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.3f ms", timing.durationMS);

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.1f%%", percentage);
        }

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Total GPU Pipeline Time");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "%.3f ms", totalTime);
        ImGui::TableSetColumnIndex(2);
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "100.0%%");

        ImGui::EndTable();
    }
}

std::string RenderGraph::ExportToMermaid() const
{
    std::stringstream ss;
    ss << "graph LR\n";
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

        ss << "    " << passNode << shape << "\"" << pass.name << "\""
           << endShape << "\n";

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
                ss << nodes[i] << (i < nodes.size() - 1 ? "," : "");
            ss << " " << className << "\n";
        }
    };

    addClass(graphicsPasses, "graphics");
    addClass(computePasses, "compute");
    addClass(raytracePasses, "raytrace");

    for (int idx : readLinks)
        ss << "    linkStyle " << idx << " stroke:#00FF00,stroke-width:2px\n";
    for (int idx : writeLinks)
        ss << "    linkStyle " << idx << " stroke:#FF0000,stroke-width:2px\n";

    return ss.str();
}

VkImage RenderGraphRegistry::GetImage(RGResourceHandle h)
{
    if (h == INVALID_RESOURCE || h >= graph.m_Resources.size())
    {
        return VK_NULL_HANDLE;
    }

    return graph.m_Resources[h].image.handle;
}

const GraphImage& RenderGraph::GetImage(const std::string& name) const
{
    if (m_ResourceMap.count(name))
        return m_Resources[m_ResourceMap.at(name)].image;
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
    for (const auto& res : m_Resources)
    {
        if (res.image.handle != VK_NULL_HANDLE) names.push_back(res.name);
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
    if (vkCmdBeginDebugUtilsLabelEXT) vkCmdBeginDebugUtilsLabelEXT(cmd, &l);
}

void RenderGraph::EndPassDebugLabel(VkCommandBuffer cmd)
{
    if (vkCmdEndDebugUtilsLabelEXT) vkCmdEndDebugUtilsLabelEXT(cmd);
}

bool RenderGraph::BeginDynamicRendering(VkCommandBuffer cmd,
                                        const RenderGraphPass& pass)
{
    if (pass.colorFormats.empty() && pass.depthFormat == VK_FORMAT_UNDEFINED)
        return false;

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
            bool hasClear =
                (req.clearValue.color.float32[0] != 0.0f ||
                 req.clearValue.color.float32[1] != 0.0f ||
                 req.clearValue.color.float32[2] != 0.0f ||
                 req.clearValue.color.float32[3] != 0.0f ||
                 res.name == RS::Motion || res.name == RS::FinalColor ||
                 res.name == RS::Albedo);

            VkAttachmentLoadOp loadOp = (res.firstPass == passIdx || hasClear)
                                            ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                            : VK_ATTACHMENT_LOAD_OP_LOAD;

            VkRenderingAttachmentInfo a{
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = res.image.view,
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = loadOp,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = req.clearValue};
            colorAtts.push_back(a);
        }
    }

    VkRenderingAttachmentInfo depthAtt{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
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
                depthAtt = {
                    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .imageView = res.image.view,
                    .imageLayout =
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    .loadOp = loadOp,
                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                    .clearValue = req.clearValue};
                hasDepth = true;
                break;
            }
        }
    }
    VkRenderingInfo info{.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                         .renderArea = {{0, 0}, {pass.width, pass.height}},
                         .layerCount = 1,
                         .colorAttachmentCount = (uint32_t)colorAtts.size(),
                         .pColorAttachments = colorAtts.data(),
                         .pDepthAttachment = hasDepth ? &depthAtt : nullptr};
    vkCmdBeginRendering(cmd, &info);
    return true;
}

void RenderGraph::UpdatePersistentResources(VkCommandBuffer cmd)
{
    std::vector<VkImageMemoryBarrier2> finalBarriers;
    for (auto& res : m_Resources)
    {
        if (res.image.handle == VK_NULL_HANDLE) continue;
        bool isDepth = VulkanUtils::IsDepthFormat(res.desc.format);
        VkImageAspectFlags aspectMask =
            isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

        if (!res.historyName.empty())
        {
            if (m_HistoryResources.find(res.historyName) ==
                m_HistoryResources.end())
            {
                VkImageUsageFlags histUsage = res.desc.usage |
                                              VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                              VK_IMAGE_USAGE_SAMPLED_BIT;
                if (!isDepth)
                    histUsage |= VK_IMAGE_USAGE_STORAGE_BIT;
                else
                {
                    histUsage &= ~VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
                    histUsage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                }

                GraphImage historyImg = ResourceManager::Get().CreateGraphImage(
                    res.desc.width, res.desc.height, res.desc.format, histUsage,
                    VK_IMAGE_LAYOUT_UNDEFINED, res.desc.samples,
                    "History_" + res.historyName);
                m_HistoryResources[res.historyName] = {
                    historyImg,
                    {VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_2_NONE,
                     VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT}};
            }

            auto& histRecord = m_HistoryResources[res.historyName];
            if (res.image.handle == histRecord.image.handle)
            {
                res.currentState = {VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    VK_ACCESS_2_SHADER_READ_BIT,
                                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT};
                histRecord.state = res.currentState;
                continue;
            }

            VkImageMemoryBarrier2 srcB{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = res.currentState.stage,
                .srcAccessMask = res.currentState.access,
                .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
                .oldLayout = res.currentState.layout,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = res.image.handle,
                .subresourceRange = {aspectMask, 0, 1, 0, 1}};
            VkImageMemoryBarrier2 dstB{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = histRecord.state.stage,
                .srcAccessMask = histRecord.state.access,
                .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .oldLayout = histRecord.state.layout,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = histRecord.image.handle,
                .subresourceRange = {aspectMask, 0, 1, 0, 1}};

            VkImageMemoryBarrier2 copyBs[] = {srcB, dstB};
            VkDependencyInfo copyDep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                     .imageMemoryBarrierCount = 2,
                                     .pImageMemoryBarriers = copyBs};
            vkCmdPipelineBarrier2(cmd, &copyDep);

            VkImageCopy region{{aspectMask, 0, 0, 1},
                               {0, 0, 0},
                               {aspectMask, 0, 0, 1},
                               {0, 0, 0},
                               {res.desc.width, res.desc.height, 1}};
            vkCmdCopyImage(cmd, res.image.handle,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           histRecord.image.handle,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            VkImageMemoryBarrier2 postSrcB = srcB;
            postSrcB.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            postSrcB.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            postSrcB.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            postSrcB.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            postSrcB.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            postSrcB.newLayout =
                isDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkImageMemoryBarrier2 postDstB = dstB;
            postDstB.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            postDstB.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            postDstB.dstStageMask =
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
            postDstB.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            postDstB.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            postDstB.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkImageMemoryBarrier2 postBs[] = {postSrcB, postDstB};
            VkDependencyInfo postDep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                     .imageMemoryBarrierCount = 2,
                                     .pImageMemoryBarriers = postBs};
            vkCmdPipelineBarrier2(cmd, &postDep);

            res.currentState = {postSrcB.newLayout, VK_ACCESS_2_SHADER_READ_BIT,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT};
            histRecord.state = {postDstB.newLayout, VK_ACCESS_2_SHADER_READ_BIT,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT};
        }
        else if (!res.image.is_external && !isDepth &&
                 res.currentState.layout !=
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            VkImageMemoryBarrier2 b{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = res.currentState.stage,
                .srcAccessMask = res.currentState.access,
                .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
                .oldLayout = res.currentState.layout,
                .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = res.image.handle,
                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
            finalBarriers.push_back(b);
            res.currentState = {VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                VK_ACCESS_2_SHADER_READ_BIT,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT};
        }
    }

    if (!finalBarriers.empty())
    {
        VkDependencyInfo dep{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = (uint32_t)finalBarriers.size(),
            .pImageMemoryBarriers = finalBarriers.data()};
        vkCmdPipelineBarrier2(cmd, &dep);
    }
}
} // namespace Chimera
