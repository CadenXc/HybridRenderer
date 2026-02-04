#include "pch.h"
#include "RenderGraph.h"
#include "GraphicsExecutionContext.h"
#include "RaytracingExecutionContext.h"
#include "ComputeExecutionContext.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Backend/PipelineManager.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/Backend/VulkanCommon.h"
#include "Utils/VulkanBarrier.h"
#include "Renderer/Graph/ResourceNames.h"
#include <deque>
#include <imgui.h>

namespace Chimera {

    RenderGraph::RenderGraph(VulkanContext& context, ResourceManager& resourceManager, PipelineManager& pipelineManager)
        : m_Context(context), m_ResourceManager(resourceManager), m_PipelineManager(pipelineManager) 
    {
        VkQueryPoolCreateInfo queryPoolInfo{};
        queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        queryPoolInfo.queryCount = 128; 

        if (vkCreateQueryPool(m_Context.GetDevice(), &queryPoolInfo, nullptr, &m_TimestampQueryPool) != VK_SUCCESS) {
            CH_CORE_ERROR("RenderGraph: Failed to create timestamp query pool!");
        }
    }

    RenderGraph::~RenderGraph() 
    {
        DestroyResources();
        if (m_TimestampQueryPool != VK_NULL_HANDLE) {
            vkDestroyQueryPool(m_Context.GetDevice(), m_TimestampQueryPool, nullptr);
        }
    }

    void RenderGraph::DestroyResources() 
    {
        vkDeviceWaitIdle(m_Context.GetDevice());

        for (auto& [name, image] : m_Images) {
            if (!image.is_external) {
                m_ResourceManager.DestroyGraphImage(image);
            }
        }
        m_Images.clear();

        for (auto& [name, renderPass] : m_Passes) {
            if (std::holds_alternative<GraphicsPass>(renderPass.pass)) {
                auto& graphicsPass = std::get<GraphicsPass>(renderPass.pass);
                vkDestroyRenderPass(m_Context.GetDevice(), graphicsPass.handle, nullptr);
                for (auto framebuffer : graphicsPass.framebuffers) {
                    vkDestroyFramebuffer(m_Context.GetDevice(), framebuffer, nullptr);
                }
            }
        }
        m_Passes.clear();

        if (m_SharedMemory != VK_NULL_HANDLE) {
            vkFreeMemory(m_Context.GetDevice(), m_SharedMemory, nullptr);
            m_SharedMemory = VK_NULL_HANDLE;
        }
    }

    void RenderGraph::AddGraphicsPass(const std::string& renderPassName, std::vector<TransientResource> dependencies,
        std::vector<TransientResource> outputs, std::vector<GraphicsPipelineDescription> pipelines,
        GraphicsPassCallback callback) 
    {
        RenderPassDescription desc;
        desc.name = renderPassName;
        desc.dependencies = dependencies;
        desc.outputs = outputs;
        desc.description = GraphicsPassDescription{ pipelines, callback };
        m_PassDescriptions[renderPassName] = desc;

        for (auto& res : dependencies) m_Readers[res.name].push_back(renderPassName);
        for (auto& res : outputs) m_Writers[res.name].push_back(renderPassName);
    }

    void RenderGraph::AddRaytracingPass(const std::string& renderPassName, std::vector<TransientResource> dependencies,
        std::vector<TransientResource> outputs, RaytracingPipelineDescription pipeline,
        RaytracingPassCallback callback) 
    {
        RenderPassDescription desc;
        desc.name = renderPassName;
        desc.dependencies = dependencies;
        desc.outputs = outputs;
        desc.description = RaytracingPassDescription{ pipeline, callback };
        m_PassDescriptions[renderPassName] = desc;

        for (auto& res : dependencies) m_Readers[res.name].push_back(renderPassName);
        for (auto& res : outputs) m_Writers[res.name].push_back(renderPassName);
    }

    void RenderGraph::AddComputePass(const std::string& renderPassName, std::vector<TransientResource> dependencies,
        std::vector<TransientResource> outputs, ComputePipelineDescription pipeline, ComputePassCallback callback) 
    {
        RenderPassDescription desc;
        desc.name = renderPassName;
        desc.dependencies = dependencies;
        desc.outputs = outputs;
        desc.description = ComputePassDescription{ pipeline, callback };
        m_PassDescriptions[renderPassName] = desc;

        for (auto& res : dependencies) m_Readers[res.name].push_back(renderPassName);
        for (auto& res : outputs) m_Writers[res.name].push_back(renderPassName);
    }

    void RenderGraph::AddBlitPass(const std::string& renderPassName, const std::string& srcImageName, const std::string& dstImageName) 
    {
        RenderPassDescription desc;
        desc.name = renderPassName;
        // Mark as dependencies/outputs for lifetime tracking. Use a neutral format for validation.
        desc.dependencies.push_back(TransientResource::Image(srcImageName, VK_FORMAT_R8G8B8A8_UNORM, 0));
        desc.outputs.push_back(TransientResource::Image(dstImageName, VK_FORMAT_R8G8B8A8_UNORM, 0));
        desc.description = BlitPassDescription{};
        m_PassDescriptions[renderPassName] = desc;

        m_Passes[renderPassName] = RenderPass{ .name = renderPassName, .pass = BlitPass{ srcImageName, dstImageName } };
        m_Readers[srcImageName].push_back(renderPassName);
        m_Writers[dstImageName].push_back(renderPassName);
    }

    void RenderGraph::Build() 
    {
        FindExecutionOrder();

        // 1. Calculate Lifetimes
        for (uint32_t i = 0; i < m_ExecutionOrder.size(); ++i) {
            auto& passName = m_ExecutionOrder[i];
            auto& desc = m_PassDescriptions[passName];
            for (auto& res : desc.dependencies) {
                m_ResourceLifetimes[res.name].first_pass = std::min(m_ResourceLifetimes[res.name].first_pass, i);
                m_ResourceLifetimes[res.name].last_pass = std::max(m_ResourceLifetimes[res.name].last_pass, i);
            }
            for (auto& res : desc.outputs) {
                m_ResourceLifetimes[res.name].first_pass = std::min(m_ResourceLifetimes[res.name].first_pass, i);
                m_ResourceLifetimes[res.name].last_pass = std::max(m_ResourceLifetimes[res.name].last_pass, i);
            }
        }

        // 2. Physical Resource Creation
        CH_CORE_TRACE("RenderGraph: Building physical resources...");
        for (auto& [name, lifetime] : m_ResourceLifetimes) {
            if (name == "RENDER_OUTPUT") continue;
            if (m_Images.count(name)) continue; 

            CH_CORE_TRACE("RenderGraph: Analyzing resource '{0}'", name);

            std::string passName = "";
            if (m_Writers.count(name) && !m_Writers[name].empty()) passName = m_Writers[name][0];
            else if (m_Readers.count(name) && !m_Readers[name].empty()) passName = m_Readers[name][0];

            if (passName.empty()) {
                CH_CORE_WARN("RenderGraph: Resource '{0}' is orphaned!", name);
                continue;
            }

            if (m_PassDescriptions.find(passName) == m_PassDescriptions.end()) {
                CH_CORE_ERROR("RenderGraph: Pass '{0}' not found for resource '{1}'", passName, name);
                continue;
            }

            auto& passDesc = m_PassDescriptions[passName];
            TransientResource* targetRes = nullptr;
            for (auto& res : passDesc.dependencies) if (res.name == name) targetRes = &res;
            for (auto& res : passDesc.outputs) if (res.name == name) targetRes = &res;

            if (targetRes && targetRes->type == TransientResourceType::Image) {
                uint32_t w = targetRes->image.width;
                uint32_t h = targetRes->image.height;
                if (w == 0 || h == 0) {
                    w = m_Context.GetSwapChainExtent().width;
                    h = m_Context.GetSwapChainExtent().height;
                }

                VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                if (targetRes->image.type == TransientImageType::AttachmentImage) {
                    usage |= VulkanUtils::IsDepthFormat(targetRes->image.format) ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
                } else if (targetRes->image.type == TransientImageType::StorageImage) {
                    usage |= VK_IMAGE_USAGE_STORAGE_BIT;
                }

                CH_CORE_TRACE("RenderGraph: Allocation -> {0} ({1}x{2}) Format: {3}", name, w, h, (int)targetRes->image.format);
                m_Images[name] = m_ResourceManager.CreateGraphImage(w, h, targetRes->image.format, usage, VK_IMAGE_LAYOUT_UNDEFINED, targetRes->image.multisampled ? m_Context.GetMSAASamples() : VK_SAMPLE_COUNT_1_BIT);
                m_ImageAccess[name] = { VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT };
            }
        }

        // 3. Backend Implementation Creation
        for (auto& passName : m_ExecutionOrder) {
            auto& desc = m_PassDescriptions[passName];
            CH_CORE_TRACE("RenderGraph: Initializing Backend Pass '{0}'", passName);
            
            if (std::holds_alternative<GraphicsPassDescription>(desc.description)) CreateGraphicsPass(desc);
            else if (std::holds_alternative<RaytracingPassDescription>(desc.description)) CreateRaytracingPass(desc);
            else if (std::holds_alternative<ComputePassDescription>(desc.description)) CreateComputePass(desc);
        }
    }

    void RenderGraph::Execute(VkCommandBuffer commandBuffer, uint32_t resourceIdx, uint32_t imageIdx, std::function<void(VkCommandBuffer)> uiDrawCallback) 
    {
        if (m_ExecutionOrder.empty()) {
            if (uiDrawCallback) uiDrawCallback(commandBuffer);
            return;
        }

        VkImage swapImage = m_Context.GetSwapChainImages()[imageIdx];
        VulkanUtils::InsertImageBarrier(commandBuffer, swapImage, 
            VK_IMAGE_ASPECT_COLOR_BIT, 
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, VK_ACCESS_TRANSFER_WRITE_BIT);
        
        VkClearColorValue clearColor = { 0.1f, 0.1f, 0.1f, 1.0f };
        VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdClearColorImage(commandBuffer, swapImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);

        VulkanUtils::InsertImageBarrier(commandBuffer, swapImage, 
            VK_IMAGE_ASPECT_COLOR_BIT, 
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

        if (m_TimestampQueryPool != VK_NULL_HANDLE) {
            vkCmdResetQueryPool(commandBuffer, m_TimestampQueryPool, 0, 128);
        }

        for (int i = 0; i < (int)m_ExecutionOrder.size(); ++i) {
            std::string& passName = m_ExecutionOrder[i];
            RenderPass& renderPass = m_Passes[passName];
            
            if (std::holds_alternative<GraphicsPass>(renderPass.pass)) {
                if (m_TimestampQueryPool != VK_NULL_HANDLE) vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, m_TimestampQueryPool, (i * 2));
                InsertBarriers(commandBuffer, renderPass, imageIdx);
                ExecuteGraphicsPass(commandBuffer, resourceIdx, imageIdx, renderPass);
                if (m_TimestampQueryPool != VK_NULL_HANDLE) vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, m_TimestampQueryPool, (i * 2) + 1);
            } else if (std::holds_alternative<RaytracingPass>(renderPass.pass)) {
                if (m_TimestampQueryPool != VK_NULL_HANDLE) vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, m_TimestampQueryPool, (i * 2));
                InsertBarriers(commandBuffer, renderPass, imageIdx);
                ExecuteRaytracingPass(commandBuffer, resourceIdx, renderPass);
                if (m_TimestampQueryPool != VK_NULL_HANDLE) vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, m_TimestampQueryPool, (i * 2) + 1);
            } else if (std::holds_alternative<ComputePass>(renderPass.pass)) {
                if (m_TimestampQueryPool != VK_NULL_HANDLE) vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, m_TimestampQueryPool, (i * 2));
                InsertBarriers(commandBuffer, renderPass, imageIdx);
                ExecuteComputePass(commandBuffer, resourceIdx, renderPass);
                if (m_TimestampQueryPool != VK_NULL_HANDLE) vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, m_TimestampQueryPool, (i * 2) + 1);
            } else if (std::holds_alternative<BlitPass>(renderPass.pass)) {
                InsertBarriers(commandBuffer, renderPass, imageIdx);
                BlitPass& blit = std::get<BlitPass>(renderPass.pass);
                
                VkImage srcImg = m_Images[blit.srcName].handle;
                VkImage dstImg = (blit.dstName == "RENDER_OUTPUT") ? m_Context.GetSwapChainImages()[imageIdx] : m_Images[blit.dstName].handle;
                
                uint32_t srcW = m_Images[blit.srcName].width;
                uint32_t srcH = m_Images[blit.srcName].height;
                uint32_t dstW = (blit.dstName == "RENDER_OUTPUT") ? m_Context.GetSwapChainExtent().width : m_Images[blit.dstName].width;
                uint32_t dstH = (blit.dstName == "RENDER_OUTPUT") ? m_Context.GetSwapChainExtent().height : m_Images[blit.dstName].height;

                if (srcImg != VK_NULL_HANDLE && dstImg != VK_NULL_HANDLE) {
                    VkImageBlit blitRegion{};
                    blitRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                    blitRegion.srcOffsets[0] = { 0, 0, 0 };
                    blitRegion.srcOffsets[1] = { (int32_t)srcW, (int32_t)srcH, 1 };
                    
                    blitRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                    blitRegion.dstOffsets[0] = { 0, 0, 0 };
                    blitRegion.dstOffsets[1] = { (int32_t)dstW, (int32_t)dstH, 1 };

                    vkCmdBlitImage(commandBuffer, srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blitRegion, VK_FILTER_LINEAR);
                    
                    if (blit.dstName == "RENDER_OUTPUT") {
                        VulkanUtils::InsertImageBarrier(commandBuffer, dstImg, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
                    }
                }
            }
        }
        if (uiDrawCallback) uiDrawCallback(commandBuffer);
    }

    void RenderGraph::InsertBarriers(VkCommandBuffer commandBuffer, RenderPass& renderPass, uint32_t imageIdx) {
        RenderPassDescription& passDescription = m_PassDescriptions[renderPass.name];
        bool isGraphicsPass = std::holds_alternative<GraphicsPass>(renderPass.pass);
        bool isComputePass = std::holds_alternative<ComputePass>(renderPass.pass);
        bool isRaytracingPass = std::holds_alternative<RaytracingPass>(renderPass.pass);
        bool isBlitPass = std::holds_alternative<BlitPass>(renderPass.pass);

        auto getRequiredLayout = [&](const TransientResource& resource, bool isOutput) {
            if (isOutput) {
                if (VulkanUtils::IsDepthFormat(resource.image.format)) return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }
            
            switch (resource.type) {
                case TransientResourceType::Sampler: return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                case TransientResourceType::Storage: return VK_IMAGE_LAYOUT_GENERAL;
                case TransientResourceType::Image:   return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                default: return VK_IMAGE_LAYOUT_GENERAL;
            }
        };

        auto processResource = [&](const TransientResource& resource, bool isOutput) {
            if (resource.type != TransientResourceType::Image) return;
            
            ImageAccess currentAccess;
            VkImage imgHandle = VK_NULL_HANDLE;

            if (resource.name == "RENDER_OUTPUT") {
                currentAccess = { VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT };
                imgHandle = m_Context.GetSwapChainImages()[imageIdx];
            } else {
                if (m_ImageAccess.find(resource.name) == m_ImageAccess.end()) return;
                currentAccess = m_ImageAccess[resource.name];
                imgHandle = m_Images[resource.name].handle;
            }

            VkImageLayout dstLayout = getRequiredLayout(resource, isOutput);
            
            if (isBlitPass) {
                BlitPass& blit = std::get<BlitPass>(renderPass.pass);
                if (resource.name == blit.srcName) dstLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                else if (resource.name == blit.dstName) dstLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            }

            if (currentAccess.layout != dstLayout) {
                VkImageAspectFlags aspectFlags = VulkanUtils::IsDepthFormat(resource.image.format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
                
                VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                VkAccessFlags dstAccess = isOutput ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT;

                if (isGraphicsPass) {
                    dstStage = isOutput ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                    if (isOutput) dstAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                } else if (isRaytracingPass) {
                    dstStage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
                } else if (isComputePass) {
                    dstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                }

                if (dstLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) { dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT; dstAccess = VK_ACCESS_TRANSFER_READ_BIT; }
                if (dstLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) { dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT; dstAccess = VK_ACCESS_TRANSFER_WRITE_BIT; }

                VulkanUtils::InsertImageBarrier(commandBuffer, imgHandle, aspectFlags, currentAccess.layout, dstLayout, currentAccess.stage_flags, dstStage, currentAccess.access_flags, dstAccess);
                m_ImageAccess[resource.name] = { dstLayout, dstAccess, dstStage };
            }
        };

        if (isBlitPass) {
            BlitPass& blit = std::get<BlitPass>(renderPass.pass);
            TransientResource srcRes = TransientResource::Image(blit.srcName, VK_FORMAT_B8G8R8A8_UNORM, 0); // Format doesn't matter for check
            TransientResource dstRes = TransientResource::Image(blit.dstName, VK_FORMAT_B8G8R8A8_UNORM, 0);
            processResource(srcRes, false);
            processResource(dstRes, true);
        } else {
            for (auto& res : passDescription.dependencies) processResource(res, false);
            for (auto& res : passDescription.outputs) processResource(res, true);
        }
    }

    void RenderGraph::ExecuteGraphicsPass(VkCommandBuffer commandBuffer, uint32_t resourceIdx, uint32_t imageIdx, RenderPass& renderPass) 
    {
        GraphicsPass& graphicsPass = std::get<GraphicsPass>(renderPass.pass);
        bool writesToRenderOutput = false;
        for (auto& attachment : graphicsPass.attachments) { if (attachment.name == "RENDER_OUTPUT") { writesToRenderOutput = true; break; } }
        
        uint32_t fbIdx = writesToRenderOutput ? imageIdx : 0;
        VkFramebuffer framebuffer = graphicsPass.framebuffers[fbIdx];
        uint32_t passWidth = graphicsPass.attachments[0].image.width; 
        uint32_t passHeight = graphicsPass.attachments[0].image.height;
        if (passWidth == 0 || passHeight == 0) {
            passWidth = m_Context.GetSwapChainExtent().width;
            passHeight = m_Context.GetSwapChainExtent().height;
        }

        VkRenderPassBeginInfo renderPassBeginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        renderPassBeginInfo.renderPass = graphicsPass.handle;
        renderPassBeginInfo.framebuffer = framebuffer;
        renderPassBeginInfo.renderArea.extent = { passWidth, passHeight };

        std::vector<VkClearValue> clearValues;
        for (TransientResource& attachment : graphicsPass.attachments) clearValues.emplace_back(attachment.image.clear_value);
        renderPassBeginInfo.clearValueCount = (uint32_t)clearValues.size();
        renderPassBeginInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        graphicsPass.callback([&](std::string pipelineName, GraphicsExecutionCallback executePipeline) {
            GraphicsPipeline* pipeline = m_GraphicsPipelines[pipelineName];
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->handle);
            GraphicsExecutionContext executionContext(commandBuffer, m_Context, m_ResourceManager, *pipeline);
            executionContext.BindGlobalSet(0, resourceIdx);
            if (renderPass.descriptor_set != VK_NULL_HANDLE) executionContext.BindPassSet(1, renderPass.descriptor_set);
            executePipeline(executionContext);
        });
        vkCmdEndRenderPass(commandBuffer);
    }

    void RenderGraph::ExecuteRaytracingPass(VkCommandBuffer commandBuffer, uint32_t resourceIdx, RenderPass& renderPass) 
    {
        RaytracingPass& raytracingPass = std::get<RaytracingPass>(renderPass.pass);
        raytracingPass.callback([&](std::string pipelineName, RaytracingExecutionCallback executePipeline) {
            RaytracingPipeline* pipeline = m_RaytracingPipelines[pipelineName];
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline->handle);
            RaytracingExecutionContext executionContext(commandBuffer, m_Context, m_ResourceManager, *pipeline);
            executionContext.BindGlobalSet(0, resourceIdx);
            if (renderPass.descriptor_set != VK_NULL_HANDLE) executionContext.BindPassSet(1, renderPass.descriptor_set);
            executePipeline(executionContext);
        });
    }

    void RenderGraph::ExecuteComputePass(VkCommandBuffer commandBuffer, uint32_t resourceIdx, RenderPass& renderPass) 
    {
        ComputePass& computePass = std::get<ComputePass>(renderPass.pass);
        ComputeExecutionContext executionContext(commandBuffer, renderPass, *this, m_ResourceManager, resourceIdx);
        computePass.callback(executionContext);
    }

    void RenderGraph::FindExecutionOrder() 
    {
        std::string finalTarget = m_Writers.count("FinalColor") ? "FinalColor" : (m_Writers.count("RENDER_OUTPUT") ? "RENDER_OUTPUT" : "");
        if (finalTarget.empty()) return;

        m_ExecutionOrder.clear();
        std::deque<std::string> stack;
        for (const auto& passName : m_Writers[finalTarget]) {
            m_ExecutionOrder.push_back(passName);
            stack.push_back(passName);
        }

        std::unordered_set<std::string> visited;
        while (!stack.empty()) {
            std::string currentPass = stack.front();
            stack.pop_front();
            if (visited.count(currentPass)) continue;
            visited.insert(currentPass);

            for (auto& dep : m_PassDescriptions[currentPass].dependencies) {
                for (auto& writerPass : m_Writers[dep.name]) {
                    if (!visited.count(writerPass)) {
                        m_ExecutionOrder.push_back(writerPass);
                        stack.push_back(writerPass);
                    }
                }
            }
        }
        std::reverse(m_ExecutionOrder.begin(), m_ExecutionOrder.end());
        
        std::vector<std::string> uniqueOrder;
        std::unordered_set<std::string> seen;
        for (auto& p : m_ExecutionOrder) if (seen.find(p) == seen.end()) { uniqueOrder.push_back(p); seen.insert(p); }
        m_ExecutionOrder = uniqueOrder;
    }

    void RenderGraph::CreateGraphicsPass(RenderPassDescription& passDescription) {
        GraphicsPassDescription& graphicsPassDescription = std::get<GraphicsPassDescription>(passDescription.description);
        RenderPass renderPass{ .name = passDescription.name, .pass = GraphicsPass { .callback = graphicsPassDescription.callback } };
        GraphicsPass& graphicsPass = std::get<GraphicsPass>(renderPass.pass);
        std::vector<VkAttachmentDescription> attachments; std::vector<VkAttachmentReference> colorRefs; VkAttachmentReference depthRef{ VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED }; bool isMultisampled = false;
        ParseGraphicsAttachments(passDescription, graphicsPass, attachments, colorRefs, depthRef, isMultisampled);
        VkSubpassDescription subpass{ 0, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, nullptr, (uint32_t)colorRefs.size(), colorRefs.data(), nullptr, (depthRef.attachment == VK_ATTACHMENT_UNUSED) ? nullptr : &depthRef, 0, nullptr };
        VkSubpassDependency dependency{ VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0 };
        VkRenderPassCreateInfo rpInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, nullptr, 0, (uint32_t)attachments.size(), attachments.data(), 1, &subpass, 1, &dependency };
        if (vkCreateRenderPass(m_Context.GetDevice(), &rpInfo, nullptr, &graphicsPass.handle) != VK_SUCCESS) throw std::runtime_error("failed to create render pass");
        CreatePassDescriptorSet(renderPass, passDescription, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
        for (auto& pd : graphicsPassDescription.pipeline_descriptions) m_GraphicsPipelines[pd.name] = &m_PipelineManager.GetGraphicsPipeline(renderPass, pd);
        CreateFramebuffers(renderPass); m_Passes[renderPass.name] = renderPass;
    }

    void RenderGraph::CreateRaytracingPass(RenderPassDescription& passDescription) {
        RaytracingPassDescription& rtDesc = std::get<RaytracingPassDescription>(passDescription.description);
        RenderPass renderPass{ .name = passDescription.name, .pass = RaytracingPass { .callback = rtDesc.callback } };
        CreatePassDescriptorSet(renderPass, passDescription, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR);
        m_RaytracingPipelines[rtDesc.pipeline_description.name] = &m_PipelineManager.GetRaytracingPipeline(renderPass, rtDesc.pipeline_description);
        m_Passes[renderPass.name] = renderPass;
    }

    void RenderGraph::CreateComputePass(RenderPassDescription& passDescription) {
        ComputePassDescription& compDesc = std::get<ComputePassDescription>(passDescription.description);
        RenderPass renderPass{ .name = passDescription.name, .pass = ComputePass { .callback = compDesc.callback } };
        CreatePassDescriptorSet(renderPass, passDescription, VK_SHADER_STAGE_COMPUTE_BIT);
        for (auto& kernel : compDesc.pipeline_description.kernels) m_ComputePipelines[kernel.shader] = &m_PipelineManager.GetComputePipeline(renderPass, compDesc.pipeline_description.push_constant_description, kernel);
        m_Passes[renderPass.name] = renderPass;
    }

    void RenderGraph::CreateFramebuffers(RenderPass& renderPass) {
        if (!std::holds_alternative<GraphicsPass>(renderPass.pass)) return;
        GraphicsPass& graphicsPass = std::get<GraphicsPass>(renderPass.pass);
        bool writesToRenderOutput = false;
        for (auto& attachment : graphicsPass.attachments) { if (attachment.name == "RENDER_OUTPUT") { writesToRenderOutput = true; break; } }
        uint32_t framebufferCount = writesToRenderOutput ? m_Context.GetSwapChainImageCount() : 1;
        graphicsPass.framebuffers.resize(framebufferCount);
        for (uint32_t i = 0; i < framebufferCount; i++) {
            std::vector<VkImageView> imageViews; bool isMultisampledPass = false;
            for (TransientResource& attachment : graphicsPass.attachments) {
                if (attachment.name == "RENDER_OUTPUT") {
                    if (attachment.image.multisampled) { imageViews.emplace_back(m_Images[renderPass.name + "_MSAA"].view); isMultisampledPass = true; }
                    else imageViews.emplace_back(m_Context.GetSwapChainImageViews()[i]);
                } else imageViews.emplace_back(m_Images[attachment.name].view);
            }
            if (isMultisampledPass) imageViews.emplace_back(m_Context.GetSwapChainImageViews()[i]);
            uint32_t passWidth = graphicsPass.attachments[0].image.width; uint32_t passHeight = graphicsPass.attachments[0].image.height;
            if (passWidth == 0 || passHeight == 0) { passWidth = m_Context.GetSwapChainExtent().width; passHeight = m_Context.GetSwapChainExtent().height; }
            VkFramebufferCreateInfo framebufferInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr, 0, graphicsPass.handle, (uint32_t)imageViews.size(), imageViews.data(), passWidth, passHeight, 1 };
            if (vkCreateFramebuffer(m_Context.GetDevice(), &framebufferInfo, nullptr, &graphicsPass.framebuffers[i]) != VK_SUCCESS) throw std::runtime_error("failed to create framebuffer");
        }
    }

    void RenderGraph::CreatePassDescriptorSet(RenderPass& renderPass, RenderPassDescription& passDescription, VkShaderStageFlags stageFlags) 
    {
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        std::vector<VkDescriptorImageInfo> imgInfos;
        std::vector<VkDescriptorBufferInfo> bufInfos;
        std::vector<VkWriteDescriptorSetAccelerationStructureKHR> asInfos;

        auto processResource = [&](TransientResource& res) {
            if (res.type == TransientResourceType::Image && res.image.type != TransientImageType::AttachmentImage) {
                VkDescriptorSetLayoutBinding b{};
                b.binding = res.image.binding;
                b.descriptorType = (res.image.type == TransientImageType::StorageImage) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                b.descriptorCount = 1;
                b.stageFlags = stageFlags;
                bindings.push_back(b);

                VkDescriptorImageInfo info{};
                info.sampler = m_ResourceManager.GetDefaultSampler();
                info.imageView = m_Images[res.name].view;
                info.imageLayout = (res.image.type == TransientImageType::StorageImage) ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imgInfos.push_back(info);
            } 
            else if (res.type == TransientResourceType::Buffer) {
                VkDescriptorSetLayoutBinding b{};
                b.binding = res.buffer.binding;
                b.descriptorType = res.buffer.descriptor_type;
                b.descriptorCount = 1;
                b.stageFlags = stageFlags;
                bindings.push_back(b);

                VkDescriptorBufferInfo info{};
                info.buffer = res.buffer.handle;
                info.offset = 0;
                info.range = VK_WHOLE_SIZE;
                bufInfos.push_back(info);
            } 
            else if (res.type == TransientResourceType::AccelerationStructure) {
                VkDescriptorSetLayoutBinding b{};
                b.binding = res.as.binding;
                b.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
                b.descriptorCount = 1;
                b.stageFlags = stageFlags;
                bindings.push_back(b);

                VkWriteDescriptorSetAccelerationStructureKHR info{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
                info.accelerationStructureCount = 1;
                info.pAccelerationStructures = &res.as.handle;
                asInfos.push_back(info);
            }
        };

        for (auto& res : passDescription.dependencies) processResource(res);
        for (auto& res : passDescription.outputs) processResource(res);

        if (bindings.empty()) {
            renderPass.descriptor_set_layout = VK_NULL_HANDLE;
            renderPass.descriptor_set = VK_NULL_HANDLE;
            return;
        }

        VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        layoutInfo.bindingCount = (uint32_t)bindings.size();
        layoutInfo.pBindings = bindings.data();
        if (vkCreateDescriptorSetLayout(m_Context.GetDevice(), &layoutInfo, nullptr, &renderPass.descriptor_set_layout) != VK_SUCCESS) throw std::runtime_error("failed to create layout");

        VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocInfo.descriptorPool = m_ResourceManager.GetTransientDescriptorPool();
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &renderPass.descriptor_set_layout;
        if (vkAllocateDescriptorSets(m_Context.GetDevice(), &allocInfo, &renderPass.descriptor_set) != VK_SUCCESS) throw std::runtime_error("failed to alloc set");

        std::vector<VkWriteDescriptorSet> writes;
        int iIdx = 0, bIdx = 0, aIdx = 0;
        for (auto& b : bindings) {
            VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            w.dstSet = renderPass.descriptor_set;
            w.dstBinding = b.binding;
            w.descriptorCount = 1;
            w.descriptorType = b.descriptorType;

            if (b.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE || b.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                w.pImageInfo = &imgInfos[iIdx++];
            } else if (b.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
                w.pBufferInfo = &bufInfos[bIdx++];
            } else if (b.descriptorType == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR) {
                w.pNext = &asInfos[aIdx++];
            }
            writes.push_back(w);
        }
        vkUpdateDescriptorSets(m_Context.GetDevice(), (uint32_t)writes.size(), writes.data(), 0, nullptr);
    }

    void RenderGraph::ParseGraphicsAttachments(RenderPassDescription& passDescription, GraphicsPass& graphicsPass, 
        std::vector<VkAttachmentDescription>& attachments, std::vector<VkAttachmentReference>& colorRefs, 
        VkAttachmentReference& depthRef, bool& isMultisampled) 
    {
        uint32_t colorCount = 0;
        uint32_t totalCount = 0;
        isMultisampled = false;

        for (TransientResource& output : passDescription.outputs) {
            if (output.type == TransientResourceType::Image && output.image.type == TransientImageType::AttachmentImage) {
                if (!VulkanUtils::IsDepthFormat(output.image.format)) colorCount++;
                if (output.image.multisampled) isMultisampled = true;
                totalCount++;
            }
        }

        attachments.resize(totalCount);
        colorRefs.resize(colorCount);
        graphicsPass.attachments.resize(totalCount);

        for (TransientResource& output : passDescription.outputs) {
            if (output.type != TransientResourceType::Image || output.image.type != TransientImageType::AttachmentImage) continue;

            uint32_t binding = output.image.binding;
            bool isRenderOutput = (output.name == "RENDER_OUTPUT");
            VkImageLayout layout = VulkanUtils::GetImageLayoutFromResourceType(output.type, output.image.format);
            
            graphicsPass.attachments[binding] = output;

            VkAttachmentDescription& attDesc = attachments[binding];
            attDesc.flags = 0;
            attDesc.format = isRenderOutput ? m_Context.GetSwapChainImageFormat() : output.image.format;
            attDesc.samples = output.image.multisampled ? m_Context.GetMSAASamples() : VK_SAMPLE_COUNT_1_BIT;
            attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attDesc.finalLayout = isRenderOutput ? (output.image.multisampled ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) : layout;

            if (VulkanUtils::IsDepthFormat(output.image.format)) {
                depthRef = { binding, layout };
            } else {
                colorRefs[binding] = { binding, layout };
            }
        }
    }

    bool RenderGraph::ContainsImage(std::string imageName) { return m_Images.count(imageName); }
    VkFormat RenderGraph::GetImageFormat(std::string imageName) { return m_Images[imageName].format; }
    std::vector<std::string> RenderGraph::GetColorAttachments() {
        std::vector<std::string> colorAttachmentNames;
        for (auto& [name, image] : m_Images) if (!VulkanUtils::IsDepthFormat(image.format) && !name.ends_with("_MSAA")) colorAttachmentNames.emplace_back(name);
        return colorAttachmentNames;
    }

    void RenderGraph::RegisterExternalResource(const std::string& name, const ImageDescription& description) { m_Images[name] = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, description.width, description.height, description.format, description.usage, true }; }
    void RenderGraph::SetExternalResource(const std::string& name, VkImage handle, VkImageView view, VkImageLayout currentLayout, VkAccessFlags currentAccess, VkPipelineStageFlags currentStage) { m_Images[name].handle = handle; m_Images[name].view = view; m_ImageAccess[name] = { currentLayout, currentAccess, currentStage }; }

    void RenderGraph::GatherPerformanceStatistics() 
    {
        if (m_TimestampQueryPool == VK_NULL_HANDLE || m_ExecutionOrder.empty()) return;

        uint32_t queryCount = (uint32_t)m_ExecutionOrder.size() * 2;
        std::vector<uint64_t> timestamps(queryCount);
        VkResult result = vkGetQueryPoolResults(m_Context.GetDevice(), m_TimestampQueryPool, 0, queryCount, 
            timestamps.size() * sizeof(uint64_t), timestamps.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);

        if (result == VK_SUCCESS) {
            float timestampPeriod = m_Context.GetDeviceProperties().limits.timestampPeriod;
            for (uint32_t i = 0; i < m_ExecutionOrder.size(); ++i) {
                uint64_t start = timestamps[i * 2];
                uint64_t end = timestamps[i * 2 + 1];
                if (end > start) {
                    double duration = (end - start) * (double)timestampPeriod / 1000000.0; // ms
                    m_PassTimestamps[m_ExecutionOrder[i]] = duration;
                }
            }
        }
    }

    void RenderGraph::DrawPerformanceStatistics() 
    {
        if (ImGui::BeginTable("RenderPassStats", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Pass Name");
            ImGui::TableSetupColumn("Time (ms)");
            ImGui::TableHeadersRow();

            double totalTime = 0.0;
            for (const auto& passName : m_ExecutionOrder) {
                if (m_PassTimestamps.count(passName)) {
                    double time = m_PassTimestamps[passName];
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", passName.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%.4f", time);
                    totalTime += time;
                }
            }
            
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Total GPU Time");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%.4f ms", totalTime);

            ImGui::EndTable();
        }
    }
}
