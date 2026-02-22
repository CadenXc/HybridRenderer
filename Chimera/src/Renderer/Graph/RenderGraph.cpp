#include "pch.h"
#include "RenderGraphCommon.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/Backend/PipelineManager.h"
#include "Renderer/Backend/Renderer.h"
#include "Utils/VulkanBarrier.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Core/Application.h"
#include "Renderer/RenderState.h"
#include <imgui.h>
#include <set>
#include <numeric>

namespace Chimera
{
    VkImageView RenderGraphRegistry::GetImageView(RGResourceHandle h)
    {
        return graph.m_Resources[h].image.view;
    }

    VkImage RenderGraphRegistry::GetImage(RGResourceHandle h)
    {
        return graph.m_Resources[h].image.handle;
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

    RGResourceHandle RenderGraph::PassBuilder::Write(const std::string& name, VkFormat format)
    {
        RGResourceHandle h = graph.GetResourceHandle(name);
        if (format != VK_FORMAT_UNDEFINED)
        {
            graph.m_Resources[h].image.format = format;
        }

        bool isDepth = VulkanUtils::IsDepthFormat(graph.m_Resources[h].image.format);
        ResourceRequest req{ h, isDepth ? ResourceUsage::DepthStencilWrite : ResourceUsage::ColorAttachment };
        
        if (isDepth)
        {
            req.clearValue.depthStencil = { 0.0f, 0 };
        }
        else
        {
            req.clearValue.color = { {0.0f, 0.0f, 0.0f, 1.0f} };
        }

        pass.outputs.push_back(req);
        return h;
    }

    RGResourceHandle RenderGraph::PassBuilder::WriteStorage(const std::string& name, VkFormat format)
    {
        RGResourceHandle h = graph.GetResourceHandle(name);
        if (format != VK_FORMAT_UNDEFINED)
        {
            graph.m_Resources[h].image.format = format;
        }
        pass.outputs.push_back({ h, ResourceUsage::StorageWrite });
        return h;
    }

    RGResourceHandle RenderGraph::PassBuilder::ReadHistory(const std::string& name)
    {
        RGResourceHandle h = graph.GetResourceHandle("History_" + name);
        pass.inputs.push_back({ h, ResourceUsage::ComputeSampled });
        return h;
    }

    RenderGraph::RenderGraph(VulkanContext& ctx, uint32_t w, uint32_t h)
        : m_Context(ctx), m_Width(w), m_Height(h)
    {
        VkDevice device = m_Context.GetDevice();
        
        VkQueryPoolCreateInfo qInfo{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, nullptr, 0, VK_QUERY_TYPE_TIMESTAMP, 128 };
        vkCreateQueryPool(device, &qInfo, nullptr, &m_TimestampQueryPool);

        VkCommandPoolCreateInfo cpInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        cpInfo.queueFamilyIndex = m_Context.GetComputeQueueFamily();
        cpInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        vkCreateCommandPool(device, &cpInfo, nullptr, &m_ComputeCommandPool);

        VkCommandBufferAllocateInfo cbAlloc{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        cbAlloc.commandPool = m_ComputeCommandPool;
        cbAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbAlloc.commandBufferCount = 1;
        vkAllocateCommandBuffers(device, &cbAlloc, &m_ComputeCommandBuffer);

        VkSemaphoreCreateInfo semInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        vkCreateSemaphore(device, &semInfo, nullptr, &m_ComputeFinishedSemaphore);
        vkCreateSemaphore(device, &semInfo, nullptr, &m_GraphicsWaitSemaphore);
    }

    RenderGraph::~RenderGraph()
    {
        DestroyResources(true);
        VkDevice device = m_Context.GetDevice();

        if (m_TimestampQueryPool != VK_NULL_HANDLE)
        {
            vkDestroyQueryPool(device, m_TimestampQueryPool, nullptr);
        }
        
        if (m_ComputeCommandPool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(device, m_ComputeCommandPool, nullptr);
        }

        if (m_ComputeFinishedSemaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device, m_ComputeFinishedSemaphore, nullptr);
        }

        if (m_GraphicsWaitSemaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device, m_GraphicsWaitSemaphore, nullptr);
        }
    }

    void RenderGraph::Reset()
    {
        // Cache device and results to avoid redundant calls
        VkDevice device = m_Context.GetDevice();

        if (m_PreviousPassCount > 0)
        {
            std::vector<uint64_t> results(m_PreviousPassCount * 2);
            VkResult res = vkGetQueryPoolResults(device, m_TimestampQueryPool, 0, m_PreviousPassCount * 2,
                results.size() * sizeof(uint64_t), results.data(), sizeof(uint64_t),
                VK_QUERY_RESULT_64_BIT);

            if (res == VK_SUCCESS)
            {
                float timestampPeriod = m_Context.GetDeviceProperties().limits.timestampPeriod;
                m_LatestTimings.clear();
                for (uint32_t i = 0; i < m_PreviousPassCount; ++i)
                {
                    float duration = float(results[i * 2 + 1] - results[i * 2]) * timestampPeriod / 1000000.0f;
                    std::string name = (i < m_LastPassNames.size()) ? m_LastPassNames[i] : "Unknown";
                    m_LatestTimings.push_back({ name, duration });
                }
            }
        }

        m_PassStack.clear();
        m_Resources.clear();
        m_ResourceMap.clear();
        // --- FIX: Do NOT clear m_PhysicalImageStates here. Physical images in the pool 
        // persist their layout on the GPU across frames. ---

        // --- FIX: Reset pool reuse counters to allow intra-frame aliasing in the new frame ---
        for (auto& pooled : m_ImagePool)
        {
            pooled.lastUsedPass = -1;
        }
    }

    void RenderGraph::Compile()
    {
        // 1. Topological Sort (Simplified)
        if (!m_PassStack.empty())
        {
            std::vector<RenderPass> sortedPasses;
            std::vector<bool> visited(m_PassStack.size(), false);
            std::unordered_map<std::string, uint32_t> lastWriter;

            for (uint32_t i = 0; i < (uint32_t)m_PassStack.size(); ++i)
            {
                            for (auto& out : m_PassStack[i].outputs)
                            {
                                lastWriter[m_Resources[out.handle].name] = i;
                            }            }

            std::function<void(uint32_t)> visit = [&](uint32_t passIdx)
            {
                if (visited[passIdx])
                {
                    return;
                }
                for (auto& in : m_PassStack[passIdx].inputs)
                {
                    const std::string& resName = m_Resources[in.handle].name;
                    if (lastWriter.count(resName) && lastWriter[resName] != passIdx)
                        visit(lastWriter[resName]);
                }
                visited[passIdx] = true;
                sortedPasses.push_back(m_PassStack[passIdx]);
            };

            for (uint32_t i = 0; i < (uint32_t)m_PassStack.size(); ++i)
                visit(i);

            m_PassStack = sortedPasses;
        }

        // 2. Resource Life-time Calculation
        for (uint32_t i = 0; i < (uint32_t)m_PassStack.size(); ++i)
        {
            auto process = [&](const ResourceRequest& req)
            {
                auto& res = m_Resources[req.handle];
                res.firstPass = std::min(res.firstPass, i);
                res.lastPass = std::max(res.lastPass, i);
            };
            for (auto& r : m_PassStack[i].inputs)
            {
                process(r);
            }
            for (auto& r : m_PassStack[i].outputs)
            {
                process(r);
            }
        }

        // 3. Physical Resource Allocation with Pooling
        CH_CORE_TRACE("RenderGraph: Compiling with pool size: {}", m_ImagePool.size());
        for (auto& res : m_Resources)
        {
            // Skip external and swapchain
            if (res.isExternal || res.name == RS::RENDER_OUTPUT || res.name.find("History_") == 0)
            {
                if (res.name == RS::RENDER_OUTPUT)
                {
                    res.image.format = m_Context.GetSwapChainImageFormat();
                }

                if (res.name.find("History_") == 0)
                {
                    std::string baseName = res.name.substr(8);
                    if (m_HistoryResources.count(baseName))
                    {
                        res.image = m_HistoryResources[baseName].image;
                        res.currentState = m_HistoryResources[baseName].state;
                    }
                }
                continue;
            }

            VkFormat format = (res.image.format != VK_FORMAT_UNDEFINED) ? res.image.format : VK_FORMAT_R16G16B16A16_SFLOAT;
            VkImageUsageFlags usage = 0; 
            
            // --- FIX: Heuristic to detect resources that MUST NOT be shared within the SAME frame ---
            bool isPersistent = (res.name.find("_Temporal") != std::string::npos || 
                                res.name == RS::Normal || res.name == RS::Depth || res.name == RS::FinalColor ||
                                res.name == "TAAOutput");

            bool reused = false;
            // ALWAYS search the pool first to avoid leaking images every frame!
            for (auto& pooled : m_ImagePool)
            {
                // Correct comparison: -1 (reset) is always less than res.firstPass (>= 0)
                if (pooled.lastUsedPass < (int32_t)res.firstPass && 
                    pooled.image.format == format &&
                    pooled.image.width == m_Width &&
                    pooled.image.height == m_Height)
                {
                    res.image = pooled.image;
                    res.currentState = pooled.state;
                    
                    // If it's persistent, lock it for the entire frame duration using a large value
                    pooled.lastUsedPass = isPersistent ? 0x7FFFFFFF : (int32_t)res.lastPass;
                    reused = true;
                    CH_CORE_TRACE("RenderGraph: Reused physical image for resource '{}' [0x{:x}]", res.name, (uint64_t)res.image.handle);
                    break;
                }
            }

            if (!reused)
            {
                res.image = ResourceManager::Get().CreateGraphImage(m_Width, m_Height, format, usage, VK_IMAGE_LAYOUT_UNDEFINED, VK_SAMPLE_COUNT_1_BIT);
                
                if (res.image.handle == VK_NULL_HANDLE)
                {
                    CH_CORE_ERROR("RenderGraph: Failed to allocate physical image for resource '{}' ({}x{}, format {})", res.name, m_Width, m_Height, (int)format);
                }
                else
                {
                    CH_CORE_TRACE("RenderGraph: Allocated NEW physical image for resource '{}' [0x{:x}]", res.name, (uint64_t)res.image.handle);
                }

                res.image.debug_view = res.image.view; // [FIX] Initialize debug_view
                res.currentState = { VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT };
                
                // --- FIX: Initialize physical state for the new image ---
                m_PhysicalImageStates[res.image.handle] = res.currentState;

                // Add to pool with appropriate locking
                m_ImagePool.push_back({ res.image, res.currentState, isPersistent ? 0x7FFFFFFF : (int32_t)res.lastPass });
            }
            else
            {
                // --- FIX: Logic resource inherits the physical state of the reused image ---
                res.currentState = m_PhysicalImageStates[res.image.handle];
                CH_CORE_TRACE("RenderGraph: Reused physical image for resource '{}' [0x{:x}]", res.name, (uint64_t)res.image.handle);
            }
        }

        // 4. Update Pass Attachment Formats
        for (auto& pass : m_PassStack)
        {
            pass.colorFormats.clear();
            for (auto& out : pass.outputs)
            {
                auto& res = m_Resources[out.handle];
                if (out.usage == ResourceUsage::ColorAttachment)
                    pass.colorFormats.push_back(res.image.format);
                else if (out.usage == ResourceUsage::DepthStencilWrite)
                    pass.depthFormat = res.image.format;
            }
        }
    }

    VkSemaphore RenderGraph::Execute(VkCommandBuffer cmd)
    {
        auto swapchain = m_Context.GetSwapchain();
        uint32_t imageIndex = Renderer::Get().GetCurrentImageIndex();
        
        if (imageIndex >= (uint32_t)swapchain->GetImageViews().size())
        {
            return VK_NULL_HANDLE;
        }

        VkImage swapImage = swapchain->GetImages()[imageIndex];
        VkImageView swapView = swapchain->GetImageViews()[imageIndex];
        RGResourceHandle outHandle = GetResourceHandle(RS::RENDER_OUTPUT);

        // Update swapchain resource
        auto& outRes = m_Resources[outHandle];
        if (outRes.image.handle != swapImage)
        {
            outRes.image.handle = swapImage;
            outRes.image.view = swapView;
            outRes.image.debug_view = swapView;
            outRes.image.format = m_Context.GetSwapChainImageFormat();
            outRes.image.width = swapchain->GetExtent().width;
            outRes.image.height = swapchain->GetExtent().height;
            outRes.isExternal = true;
            
            // --- FIX: Ensure the physical state tracker knows about the swapchain image's actual layout ---
            // It was presented last frame, so it's in PRESENT_SRC_KHR.
            m_PhysicalImageStates[swapImage] = { VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 0, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT };
            outRes.currentState = m_PhysicalImageStates[swapImage];
        }

        uint32_t passCount = (uint32_t)m_PassStack.size();
        m_PreviousPassCount = passCount;
        m_LastPassNames.clear();

        vkCmdResetQueryPool(cmd, m_TimestampQueryPool, 0, 128);

        for (uint32_t i = 0; i < passCount; ++i)
        {
            auto& pass = m_PassStack[i];
            m_LastPassNames.push_back(pass.name);

            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_TimestampQueryPool, i * 2);
            
            BuildBarriers(cmd, pass);

            RenderGraphRegistry registry{ *this, pass };
            bool isGraphics = false;
            std::vector<VkRenderingAttachmentInfo> colorAtts;
            VkRenderingAttachmentInfo depthAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
            bool hasDepth = false;

            if (!pass.isCompute)
            {
                for (auto& out : pass.outputs)
                {
                    if (out.usage == ResourceUsage::ColorAttachment || out.usage == ResourceUsage::DepthStencilWrite)
                    {
                        isGraphics = true;
                        auto& res = m_Resources[out.handle];
                        if (res.image.view == VK_NULL_HANDLE) { isGraphics = false; break; }

                        if (out.usage == ResourceUsage::ColorAttachment)
                        {
                            VkRenderingAttachmentInfo info{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
                            info.imageView = res.image.view;
                            info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                            info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                            info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                            info.clearValue = out.clearValue;
                            colorAtts.push_back(info);
                        }
                        else
                        {
                            depthAtt.imageView = res.image.view;
                            depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                            depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                            depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                            depthAtt.clearValue = out.clearValue;
                            hasDepth = true;
                        }
                    }
                }
            }

            if (isGraphics && (!colorAtts.empty() || hasDepth))
            {
                uint32_t renderW = m_Width;
                uint32_t renderH = m_Height;

                // --- FIX: Clamp to the SMALLEST attachment size among all attachments ---
                for (const auto& att : colorAtts)
                {
                    // Find the resource for this view to get its dimensions
                    for (const auto& res : m_Resources)
                    {
                        if (res.image.view == att.imageView)
                        {
                            renderW = std::min(renderW, res.image.width);
                            renderH = std::min(renderH, res.image.height);
                            break;
                        }
                    }
                }
                if (hasDepth)
                {
                    for (const auto& res : m_Resources)
                    {
                        if (res.image.view == depthAtt.imageView)
                        {
                            renderW = std::min(renderW, res.image.width);
                            renderH = std::min(renderH, res.image.height);
                            break;
                        }
                    }
                }

                VkRenderingInfo renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
                renderingInfo.renderArea = { {0, 0}, { renderW, renderH } };
                renderingInfo.layerCount = 1;
                renderingInfo.colorAttachmentCount = (uint32_t)colorAtts.size();
                renderingInfo.pColorAttachments = colorAtts.data();
                renderingInfo.pDepthAttachment = hasDepth ? &depthAtt : nullptr;

                vkCmdBeginRendering(cmd, &renderingInfo);

                VkViewport vp{ 0.0f, 0.0f, (float)renderW, (float)renderH, 0.0f, 1.0f };
                VkRect2D sc{ {0, 0}, { renderW, renderH } };
                vkCmdSetViewport(cmd, 0, 1, &vp);
                vkCmdSetScissor(cmd, 0, 1, &sc);
            }

            if (pass.executeFunc)
            {
                pass.executeFunc(registry, cmd);
            }

            if (isGraphics && (!colorAtts.empty() || hasDepth))
                vkCmdEndRendering(cmd);

            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_TimestampQueryPool, i * 2 + 1);
        }

        // Final transitions and History management
        std::unordered_map<VkImage, ResourceState> finalStates;
        for (auto& res : m_Resources)
        {
            if (res.image.handle == VK_NULL_HANDLE || res.name == RS::RENDER_OUTPUT)
            {
                continue;
            }

            if (finalStates.find(res.image.handle) == finalStates.end())
            {
                if (res.currentState.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && !VulkanUtils::IsDepthFormat(res.image.format))
                {
                    VulkanUtils::TransitionImage(cmd, res.image.handle, res.currentState.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                    res.currentState = { VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT };
                }
                finalStates[res.image.handle] = res.currentState;
            }
            else
            {
                res.currentState = finalStates[res.image.handle];
            }

            // Sync back to pool
            for (auto& pooled : m_ImagePool)
            {
                if (pooled.image.handle == res.image.handle)
                {
                    pooled.state = res.currentState;
                    break;
                }
            }

            // Handle History Buffers
            if (res.name == RS::FinalColor || res.name == RS::Depth || res.name == RS::Normal || res.name == "TAAOutput")
            {
                std::string baseName = (res.name == "TAAOutput") ? "TAA" : res.name;
                m_HistoryResources[baseName] = { res.image, res.currentState };
            }
            else if (res.name.find("_TemporalColor") != std::string::npos)
            {
                // Example: Shadow_TemporalColor -> ShadowAccum
                std::string prefix = res.name.substr(0, res.name.find("_TemporalColor"));
                m_HistoryResources[prefix + "Accum"] = { res.image, res.currentState };
            }
            else if (res.name.find("_TemporalMoments") != std::string::npos)
            {
                // Example: Shadow_TemporalMoments -> ShadowMoments
                std::string prefix = res.name.substr(0, res.name.find("_TemporalMoments"));
                m_HistoryResources[prefix + "Moments"] = { res.image, res.currentState };
            }
        }

        return VK_NULL_HANDLE;
    }

    void RenderGraph::BuildBarriers(VkCommandBuffer cmd, RenderPass& pass)
    {
        std::vector<VkImageMemoryBarrier2> imageBarriers;

        auto process = [&](const ResourceRequest& req)
        {
            auto& res = m_Resources[req.handle];
            if (res.image.handle == VK_NULL_HANDLE) return;

            // --- FIX: Use copy instead of reference to avoid iterator invalidation on map resize ---
            ResourceState currentState = m_PhysicalImageStates[res.image.handle];
            
            // Initial state if first time seeing this image handle in this frame
            if (currentState.layout == VK_IMAGE_LAYOUT_UNDEFINED)
            {
                currentState = res.currentState; 
            }

            VkImageLayout targetLayout;
            VkAccessFlags2 targetAccess;
            VkPipelineStageFlags2 targetStage;

            switch (req.usage)
            {
                case ResourceUsage::GraphicsSampled:
                    targetLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    targetAccess = VK_ACCESS_2_SHADER_READ_BIT;
                    targetStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                    break;
                case ResourceUsage::ComputeSampled:
                    targetLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    targetAccess = VK_ACCESS_2_SHADER_READ_BIT;
                    targetStage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                    break;
                case ResourceUsage::RaytraceSampled:
                    targetLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    targetAccess = VK_ACCESS_2_SHADER_READ_BIT;
                    targetStage = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
                    break;
                case ResourceUsage::StorageRead:
                case ResourceUsage::StorageWrite:
                case ResourceUsage::StorageReadWrite:
                    targetLayout = VK_IMAGE_LAYOUT_GENERAL;
                    targetAccess = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT;
                    targetStage = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                    break;
                case ResourceUsage::ColorAttachment:
                    targetLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    targetAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                    targetStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                    break;
                case ResourceUsage::DepthStencilWrite:
                    targetLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    targetAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    targetStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
                    break;
                default:
                    return;
            }

            bool isWrite = (req.usage == ResourceUsage::StorageWrite || req.usage == ResourceUsage::ColorAttachment || req.usage == ResourceUsage::DepthStencilWrite || req.usage == ResourceUsage::StorageReadWrite);
            bool wasWrite = (currentState.access & (VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT));

            if (currentState.layout != targetLayout || wasWrite || isWrite)
            {
                VkImageMemoryBarrier2 b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
                b.srcStageMask = currentState.stage;
                b.srcAccessMask = currentState.access;
                b.dstStageMask = targetStage;
                b.dstAccessMask = targetAccess;
                b.oldLayout = currentState.layout;
                b.newLayout = targetLayout;
                b.image = res.image.handle;
                b.subresourceRange = { (VkImageAspectFlags)(VulkanUtils::IsDepthFormat(res.image.format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, 1, 0, 1 };
                
                imageBarriers.push_back(b);
                
                // --- FIX: Update map entry directly ---
                m_PhysicalImageStates[res.image.handle] = { targetLayout, targetAccess, targetStage };
                res.currentState = m_PhysicalImageStates[res.image.handle]; // Sync logical resource state
            }
        };

        for (auto& r : pass.inputs) process(r);
        for (auto& r : pass.outputs) process(r);

        if (!imageBarriers.empty())
        {
            VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
            dep.imageMemoryBarrierCount = (uint32_t)imageBarriers.size();
            dep.pImageMemoryBarriers = imageBarriers.data();
            vkCmdPipelineBarrier2(cmd, &dep);
        }
    }

    RGResourceHandle RenderGraph::GetResourceHandle(const std::string& n)
    {
        if (m_ResourceMap.count(n))
        {
            return m_ResourceMap[n];
        }
        
        RGResourceHandle h = (RGResourceHandle)m_Resources.size();
        m_Resources.push_back({ n });
        m_ResourceMap[n] = h;
        return h;
    }

    void RenderGraph::DestroyResources(bool all)
    {
        uint32_t frameIdx = Renderer::HasInstance() ? Renderer::Get().GetCurrentFrameIndex() : 0;
        VkDevice device = m_Context.GetDevice();
        VmaAllocator allocator = m_Context.GetAllocator();

        std::set<VkImage> imagesToDestroy;
        std::vector<GraphImage> resourcesToFree;

        for (auto& res : m_Resources)
        {
            if (!res.isExternal && res.image.handle != VK_NULL_HANDLE)
            {
                if (imagesToDestroy.find(res.image.handle) == imagesToDestroy.end())
                {
                    imagesToDestroy.insert(res.image.handle);
                    resourcesToFree.push_back(res.image);
                }
                res.image.handle = VK_NULL_HANDLE;
                res.image.view = VK_NULL_HANDLE;
            }
        }

        if (all)
        {
            for (auto& [name, hist] : m_HistoryResources)
            {
                if (hist.image.handle != VK_NULL_HANDLE && imagesToDestroy.find(hist.image.handle) == imagesToDestroy.end())
                {
                    imagesToDestroy.insert(hist.image.handle);
                    resourcesToFree.push_back(hist.image);
                }
            }
            for (auto& pooled : m_ImagePool)
            {
                if (pooled.image.handle != VK_NULL_HANDLE && imagesToDestroy.find(pooled.image.handle) == imagesToDestroy.end())
                {
                    imagesToDestroy.insert(pooled.image.handle);
                    resourcesToFree.push_back(pooled.image);
                }
            }
        }

        for (const auto& img : resourcesToFree)
        {
            auto destroyFunc = [device, allocator, img]()
            {
                if (img.view != VK_NULL_HANDLE) vkDestroyImageView(device, img.view, nullptr);
                if (img.debug_view != VK_NULL_HANDLE && img.debug_view != img.view) vkDestroyImageView(device, img.debug_view, nullptr);
                if (img.allocation != nullptr) vmaDestroyImage(allocator, img.handle, img.allocation);
            };

            if (all)
            {
                destroyFunc();
            }
            else if (Renderer::HasInstance())
            {
                m_Context.GetDeletionQueue().PushFunction(frameIdx, destroyFunc);
            }
            else
            {
                destroyFunc();
            }
        }

        if (all)
        {
            m_Resources.clear();
            m_ResourceMap.clear();
            m_ExternalImageStates.clear();
            m_HistoryResources.clear();
            m_ImagePool.clear();
        }
    }

    void RenderGraph::SetExternalResource(const std::string& name, VkImage image, VkImageView view, VkImageLayout layout, const ImageDescription& desc)
    {
        RGResourceHandle h = GetResourceHandle(name);
        auto& res = m_Resources[h];
        res.image.handle = image;
        res.image.view = view;
        res.image.debug_view = view; // [FIX] Ensure debug_view is not null
        res.image.width = desc.width;
        res.image.height = desc.height;
        res.image.format = desc.format;
        res.image.is_external = true;
        res.isExternal = true;
        res.currentState.layout = layout;

        if (layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
        {
            res.currentState.access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            res.currentState.stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
        else if (layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            res.currentState.access = VK_ACCESS_2_SHADER_READ_BIT;
            res.currentState.stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        }
        else if (layout == VK_IMAGE_LAYOUT_GENERAL)
        {
            res.currentState.access = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
            res.currentState.stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        }

        m_PhysicalImageStates[image] = res.currentState;
    }

    bool RenderGraph::ContainsImage(const std::string& n)
    {
        return m_ResourceMap.count(n) > 0;
    }

    const GraphImage& RenderGraph::GetImage(const std::string& n) const
    {
        if (m_ResourceMap.count(n))
        {
            return m_Resources[m_ResourceMap.at(n)].image;
        }
        
        static GraphImage dummy{};
        return dummy;
    }

    std::vector<std::string> RenderGraph::GetDebuggableResources() const
    {
        std::vector<std::string> result;
        // Collect all unique resource names that have a valid physical image
        for (const auto& res : m_Resources)
        {
            if (res.image.handle != VK_NULL_HANDLE && !res.name.empty())
            {
                result.push_back(res.name);
            }
        }
        
        // Sort alphabetically for better UI usability
        std::sort(result.begin(), result.end());
        return result;
    }

    std::string RenderGraph::ExportToMermaid() const
    {
        std::string result = "graph LR\n";
        
        // 1. Define Styles
        result += "    classDef graphics fill:#2d5a27,stroke:#afff9e,stroke-width:2px,color:#fff\n";
        result += "    classDef compute fill:#2d3e5a,stroke:#9ecaff,stroke-width:2px,color:#fff\n";
        result += "    classDef raytrace fill:#5a2d2d,stroke:#ff9e9e,stroke-width:2px,color:#fff\n";
        result += "    classDef resource fill:#333,stroke:#ccc,stroke-width:1px,color:#fff,stroke-dasharray: 5 5\n";

        std::vector<std::string> graphicsPasses;
        std::vector<std::string> computePasses;
        std::vector<std::string> raytracePasses;

        // 2. Define Pass Nodes
        for (const auto& pass : m_PassStack)
        {
            std::string shape = "[";
            std::string endShape = "]";
            
            if (pass.isCompute)
            {
                shape = "{{";
                endShape = "}}";
                computePasses.push_back("Pass_" + pass.name);
            }
            else if (pass.name.find("RT") != std::string::npos)
            {
                shape = "((";
                endShape = "))";
                raytracePasses.push_back("Pass_" + pass.name);
            }
            else
            {
                graphicsPasses.push_back("Pass_" + pass.name);
            }

            // Append shader names to the label
            std::string shaderLabel = "";
            if (!pass.shaderNames.empty())
            {
                shaderLabel = "<br/>(";
                for (size_t i = 0; i < pass.shaderNames.size(); ++i)
                {
                    shaderLabel += pass.shaderNames[i] + (i < pass.shaderNames.size() - 1 ? ", " : "");
                }
                shaderLabel += ")";
            }

            result += "    Pass_" + pass.name + shape + "\"" + pass.name + shaderLabel + "\"" + endShape + "\n";
        }

        // 3. Define Resource Nodes and Edges
        std::unordered_set<std::string> handledResources;
        std::vector<std::string> resourceNodes;

        for (const auto& pass : m_PassStack)
        {
            // Outputs: Pass -> Resource
            for (const auto& out : pass.outputs)
            {
                std::string resName = m_Resources[out.handle].name;
                std::string resID = "Res_" + resName;
                
                if (handledResources.find(resID) == handledResources.end())
                {
                    result += "    " + resID + "(\"" + resName + "\")\n";
                    resourceNodes.push_back(resID);
                    handledResources.insert(resID);
                }
                
                result += "    Pass_" + pass.name + " --> " + resID + "\n";
            }

            // Inputs: Resource -> Pass
            for (const auto& in : pass.inputs)
            {
                std::string resName = m_Resources[in.handle].name;
                std::string resID = "Res_" + resName;
                std::string label = resName;

                // SPECIAL HANDLING: History Buffers (Separate nodes with special shape)
                if (resName.find("History_") == 0)
                {
                    std::string baseName = resName.substr(8);
                    resID = "Hist_" + baseName;
                    label = baseName + " (Prev)";
                    
                    if (handledResources.find(resID) == handledResources.end())
                    {
                        // Use [/ /] for Trapezoid/Parallelogram shape
                        result += "    " + resID + "[/\"" + label + "\"/]\n";
                        resourceNodes.push_back(resID);
                        handledResources.insert(resID);
                    }
                }
                else if (handledResources.find(resID) == handledResources.end())
                {
                    result += "    " + resID + "(\"" + resName + "\")\n";
                    resourceNodes.push_back(resID);
                    handledResources.insert(resID);
                }

                result += "    " + resID + " --> Pass_" + pass.name + "\n";
            }
        }

        // 4. Assign Classes at the end
        if (!graphicsPasses.empty())
        {
            result += "    class ";
            for (size_t i = 0; i < graphicsPasses.size(); ++i)
            {
                result += (i > 0 ? "," : "") + graphicsPasses[i];
            }
            result += " graphics\n";
        }
        if (!computePasses.empty())
        {
            result += "    class ";
            for (size_t i = 0; i < computePasses.size(); ++i)
            {
                result += (i > 0 ? "," : "") + computePasses[i];
            }
            result += " compute\n";
        }
        if (!raytracePasses.empty())
        {
            result += "    class ";
            for (size_t i = 0; i < raytracePasses.size(); ++i)
            {
                result += (i > 0 ? "," : "") + raytracePasses[i];
            }
            result += " raytrace\n";
        }
        if (!resourceNodes.empty())
        {
            result += "    class ";
            for (size_t i = 0; i < resourceNodes.size(); ++i)
            {
                result += (i > 0 ? "," : "") + resourceNodes[i];
            }
            result += " resource\n";
        }
        
        return result;
    }

    void RenderGraph::DrawPerformanceStatistics()
    {
        float totalMS = 0.0f;
        for (const auto& t : m_LatestTimings) totalMS += t.durationMS;

        // Upgrade to 3 columns: [Name] [Ratio Bar] [ms]
        if (ImGui::BeginTable("PassTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
        {
            ImGui::TableSetupColumn("Pass Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("GPU Usage", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableHeadersRow();

            for (const auto& t : m_LatestTimings)
            {
                ImGui::TableNextRow();
                
                // Column 0: Name
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", t.name.c_str());

                // Column 1: Progress Bar
                ImGui::TableSetColumnIndex(1);
                ImVec4 barColor = ImVec4(0.2f, 0.5f, 0.8f, 1.0f); 
                if (t.name.find("SVGF") != std::string::npos || t.name.find("Atrous") != std::string::npos)
                    barColor = ImVec4(0.2f, 0.8f, 0.4f, 1.0f); 
                else if (t.name.find("RT") != std::string::npos || t.name.find("Ray") != std::string::npos)
                    barColor = ImVec4(0.8f, 0.3f, 0.3f, 1.0f); 
                else if (t.name.find("Bloom") != std::string::npos || t.name.find("TAA") != std::string::npos)
                    barColor = ImVec4(0.8f, 0.8f, 0.2f, 1.0f); 

                float ratio = totalMS > 0.0f ? (t.durationMS / totalMS) : 0.0f;
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
                char label[32]; sprintf(label, "%.1f%%", ratio * 100.0f);
                ImGui::ProgressBar(ratio, ImVec2(-1, 14), label);
                ImGui::PopStyleColor();

                // Column 2: Raw Time
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%.3f", t.durationMS);
            }
            ImGui::EndTable();
        }
        
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Total GPU Frame Time: %.3f ms", totalMS);
    }
}
