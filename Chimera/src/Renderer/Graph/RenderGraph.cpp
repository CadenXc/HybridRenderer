#include "pch.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/Backend/PipelineManager.h"
#include "Renderer/Backend/ShaderMetadata.h"
#include "Renderer/RenderState.h"
#include "Utils/VulkanBarrier.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Core/Application.h"
#include "Scene/Scene.h"
#include <deque>
#include <imgui.h>

namespace Chimera {

    void RaytracingExecutionContext::Dispatch(uint32_t w, uint32_t h, uint32_t d) {
        // Implementation provided in PipelineManager or similar if needed
    }

    RenderGraph::RenderGraph(VulkanContext& ctx, ResourceManager& rm, PipelineManager& pm, uint32_t w, uint32_t h)
        : m_Context(ctx), m_ResourceManager(rm), m_PipelineManager(pm), m_Width(w), m_Height(h)
    {
        VkQueryPoolCreateInfo queryPoolInfo{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
        queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP; queryPoolInfo.queryCount = 128;
        vkCreateQueryPool(m_Context.GetDevice(), &queryPoolInfo, nullptr, &m_TimestampQueryPool);
    }

    RenderGraph::~RenderGraph() { DestroyResources(true); if(m_TimestampQueryPool != VK_NULL_HANDLE) vkDestroyQueryPool(m_Context.GetDevice(), m_TimestampQueryPool, nullptr); }

    void RenderGraph::DestroyResources(bool all) {
        VkDevice device = m_Context.GetDevice();
        for (auto& [name, pass] : m_Passes) {
            if (std::holds_alternative<GraphicsPass>(pass.pass)) {
                auto& gp = std::get<GraphicsPass>(pass.pass);
                for (auto fb : gp.framebuffers) vkDestroyFramebuffer(device, fb, nullptr);
                vkDestroyRenderPass(device, gp.handle, nullptr);
            }
        }
        m_Passes.clear();
        for (auto& [name, img] : m_Images) {
            if (!img.is_external || all) {
                vkDestroyImageView(device, img.view, nullptr);
                vmaDestroyImage(m_Context.GetAllocator(), img.handle, img.allocation);
            }
        }
        m_Images.clear(); m_ImageAccess.clear(); m_ExecutionOrder.clear(); m_GraphicsPipelines.clear(); m_RaytracingPipelines.clear(); m_ComputePipelines.clear(); m_SamplerArrays.clear();
    }

    void RenderGraph::AddGraphicsPass(const GraphicsPassSpecification& s) {
        RenderPassDescription d; d.name = s.Name; d.dependencies = s.Dependencies; d.outputs = s.Outputs;
        d.description = s.Pipelines[0]; d.callback = s.Callback; m_PassDescriptions[s.Name] = d;
    }

    void RenderGraph::AddRaytracingPass(const RaytracingPassSpecification& s) {
        RenderPassDescription d; d.name = s.Name; d.dependencies = s.Dependencies; d.outputs = s.Outputs;
        d.description = s.Pipeline; d.callback = s.Callback; m_PassDescriptions[s.Name] = d;
    }

    void RenderGraph::AddComputePass(const ComputePassSpecification& s) {
        RenderPassDescription d; d.name = s.Name; d.dependencies = s.Dependencies; d.outputs = s.Outputs;
        d.description = s.Pipeline; d.callback = s.Callback; m_PassDescriptions[s.Name] = d;
    }

    void RenderGraph::AddBlitPass(const std::string& n, const std::string& s, const std::string& d, VkFormat sf, VkFormat df) {
        RenderPassDescription desc; desc.name = n; desc.description = BlitPassDescription{};
        desc.dependencies.push_back(TransientResource::Image(s, sf)); desc.outputs.push_back(TransientResource::Image(d, df));
        m_PassDescriptions[n] = desc;
    }

    void RenderGraph::Build() {
        m_ExecutionOrder.clear(); for(auto& [name, desc] : m_PassDescriptions) m_ExecutionOrder.push_back(name);
        for (auto& passName : m_ExecutionOrder) {
            auto& desc = m_PassDescriptions[passName];
            auto process = [&](const TransientResource& res) {
                if (res.type == TransientResourceType::Image && res.name != RS::RENDER_OUTPUT && !m_Images.count(res.name)) {
                    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                    
                    if (VulkanUtils::IsDepthFormat(res.image.format)) {
                        usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                    } else {
                        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
                        usage |= VK_IMAGE_USAGE_STORAGE_BIT; // Only color images for Storage/RT output
                    }
                    
                    CH_CORE_INFO("RenderGraph: Creating image '{0}' ({1}x{2}, format: {3})", res.name, m_Width, m_Height, (int)res.image.format);
                    m_Images[res.name] = m_ResourceManager.CreateGraphImage(m_Width, m_Height, res.image.format, usage, VK_IMAGE_LAYOUT_UNDEFINED, VK_SAMPLE_COUNT_1_BIT);
                }
            };
            for (auto& res : desc.dependencies) process(res);
            for (auto& res : desc.outputs) process(res);
        }
        for (auto& passName : m_ExecutionOrder) {
            auto& desc = m_PassDescriptions[passName];
            if (std::holds_alternative<GraphicsPipelineDescription>(desc.description)) CreateGraphicsPass(desc);
            else if (std::holds_alternative<RaytracingPipelineDescription>(desc.description)) CreateRaytracingPass(desc);
            else if (std::holds_alternative<ComputePipelineDescription>(desc.description)) CreateComputePass(desc);
            else if (std::holds_alternative<BlitPassDescription>(desc.description)) m_Passes[passName] = { passName, VK_NULL_HANDLE, VK_NULL_HANDLE, BlitPass{desc.dependencies[0].name, desc.outputs[0].name} };
        }
    }

    void RenderGraph::Execute(VkCommandBuffer cmd, uint32_t rIdx, uint32_t iIdx) {
        // [FIX] Reset internal image layouts to UNDEFINED at start of frame
        for (auto& [name, access] : m_ImageAccess) {
            if (m_Images.count(name) && !m_Images.at(name).is_external) {
                access.layout = VK_IMAGE_LAYOUT_UNDEFINED;
                access.stage_flags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                access.access_flags = 0;
            }
        }

        for (auto& passName : m_ExecutionOrder) {
            auto& pass = m_Passes[passName];
            if (std::holds_alternative<GraphicsPass>(pass.pass)) ExecuteGraphicsPass(cmd, rIdx, iIdx, pass);
            else if (std::holds_alternative<RaytracingPass>(pass.pass)) ExecuteRaytracingPass(cmd, rIdx, pass);
            else if (std::holds_alternative<ComputePass>(pass.pass)) ExecuteComputePass(cmd, rIdx, pass);
            else if (std::holds_alternative<BlitPass>(pass.pass)) ExecuteBlitPass(cmd, pass);
        }
    }

    void RenderGraph::CreateGraphicsPass(RenderPassDescription& d) {
        GraphicsPass gp; gp.callback = std::get<GraphicsPassCallback>(d.callback);
        gp.attachments = d.outputs;
        m_Passes[d.name] = { d.name, VK_NULL_HANDLE, VK_NULL_HANDLE, gp };
        m_GraphicsPipelines[d.name] = &m_PipelineManager.GetGraphicsPipeline(m_Passes[d.name], std::get<GraphicsPipelineDescription>(d.description));
    }

    void RenderGraph::CreateRaytracingPass(RenderPassDescription& d) {
        RaytracingPass rp; rp.callback = std::get<RaytracingPassCallback>(d.callback);
        m_Passes[d.name] = { d.name, VK_NULL_HANDLE, VK_NULL_HANDLE, rp };
        m_RaytracingPipelines[d.name] = &m_PipelineManager.GetRaytracingPipeline(m_Passes[d.name], std::get<RaytracingPipelineDescription>(d.description));
    }

    void RenderGraph::CreateComputePass(RenderPassDescription& d) {
        ComputePass cp; cp.callback = std::get<ComputePassCallback>(d.callback);
        m_Passes[d.name] = { d.name, VK_NULL_HANDLE, VK_NULL_HANDLE, cp };
        m_ComputePipelines[d.name] = &m_PipelineManager.GetComputePipeline(m_Passes[d.name], std::get<ComputePipelineDescription>(d.description).kernels[0]);
    }

    void RenderGraph::ExecuteGraphicsPass(VkCommandBuffer cmd, uint32_t rIdx, uint32_t iIdx, RenderPass& p) {
        GraphicsPass& gp = std::get<GraphicsPass>(p.pass); auto& pipe = *m_GraphicsPipelines[p.name];

        std::vector<VkRenderingAttachmentInfoKHR> colorAttachments;
        VkRenderingAttachmentInfoKHR depthAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR };
        bool hasDepth = false;

        for (auto& att : gp.attachments) {
            if (att.type != TransientResourceType::Image) continue;
            auto& img = m_Images.at(att.name);
            auto& access = m_ImageAccess[att.name];

            VkImageLayout targetLayout = VulkanUtils::IsDepthFormat(img.format) ? 
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VulkanUtils::TransitionImageLayout(cmd, img.handle, img.format, access.layout, targetLayout);
            access.layout = targetLayout;

            VkRenderingAttachmentInfoKHR info{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR };
            info.imageView = img.view;
            info.imageLayout = targetLayout;
            info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            info.clearValue = att.image.clear_value;

            if (VulkanUtils::IsDepthFormat(img.format)) { depthAttachment = info; hasDepth = true; }
            else { colorAttachments.push_back(info); }
        }

        VkRenderingInfoKHR renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO_KHR };
        renderingInfo.renderArea = { {0, 0}, { m_Width, m_Height } };
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = (uint32_t)colorAttachments.size();
        renderingInfo.pColorAttachments = colorAttachments.data();
        renderingInfo.pDepthAttachment = hasDepth ? &depthAttachment : nullptr;

        vkCmdBeginRendering(cmd, &renderingInfo);
        
        VkViewport viewport{ 0.0f, 0.0f, (float)m_Width, (float)m_Height, 0.0f, 1.0f };
        VkRect2D scissor{ {0, 0}, { m_Width, m_Height } };
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe.handle);

        // Bind Descriptor Sets
        std::vector<VkDescriptorSet> sets = {
            Application::Get().GetRenderState()->GetDescriptorSet(rIdx),
            m_ResourceManager.GetSceneDescriptorSet()
        };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe.layout, 0, (uint32_t)sets.size(), sets.data(), 0, nullptr);

        ExecuteGraphicsCallback exec = [&](std::string n, GraphicsExecutionCallback cb) { 
            GraphicsExecutionContext ctx(cmd, pipe.layout); 
            cb(ctx); 
        };

        // [FIX] Actually call the pass callback!
        gp.callback(exec);

        vkCmdEndRendering(cmd);
    }

    void RenderGraph::ExecuteRaytracingPass(VkCommandBuffer cmd, uint32_t rIdx, RenderPass& p) {
        RaytracingPass& rp = std::get<RaytracingPass>(p.pass); auto& pipe = *m_RaytracingPipelines[p.name];
        ExecuteRaytracingCallback exec = [&](std::string n, RaytracingExecutionCallback cb) { RaytracingExecutionContext ctx(cmd, m_Context, m_ResourceManager, pipe); cb(ctx); };
        rp.callback(exec);
    }

    void RenderGraph::ExecuteComputePass(VkCommandBuffer cmd, uint32_t rIdx, RenderPass& p) {
        ComputePass& cp = std::get<ComputePass>(p.pass); ComputeExecutionContext ctx(cmd, p, *this, m_ResourceManager, rIdx); cp.callback(ctx);
    }

    void RenderGraph::ExecuteBlitPass(VkCommandBuffer cmd, RenderPass& p) {
        auto& bp = std::get<BlitPass>(p.pass);
        auto& src = m_Images.at(bp.srcName);
        auto& srcAccess = m_ImageAccess[bp.srcName];
        VkImage dst = VK_NULL_HANDLE;
        VkFormat dstFormat = VK_FORMAT_UNDEFINED;
        VkImageLayout currentDstLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        
        if (bp.dstName == RS::RENDER_OUTPUT) {
            dst = m_Context.GetSwapChainImages()[Application::Get().GetCurrentImageIndex()];
            dstFormat = m_Context.GetSwapChainImageFormat();
            currentDstLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // From Renderer::BeginFrame
        } else {
            dst = m_Images.at(bp.dstName).handle;
            dstFormat = m_Images.at(bp.dstName).format;
            currentDstLayout = m_ImageAccess[bp.dstName].layout;
        }

        VkImageBlit blit{};
        blit.srcOffsets[1] = { (int32_t)src.width, (int32_t)src.height, 1 };
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[1] = { (int32_t)m_Width, (int32_t)m_Height, 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.layerCount = 1;

        // Transition SRC to TRANSFER_SRC
        VulkanUtils::TransitionImageLayout(cmd, src.handle, src.format, srcAccess.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        
        // Transition DST to TRANSFER_DST
        VulkanUtils::TransitionImageLayout(cmd, dst, dstFormat, currentDstLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        
        vkCmdBlitImage(cmd, src.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        
        // After blit, we transition swapchain/dst back to COLOR_ATTACHMENT_OPTIMAL so ImGui can draw on it
        VulkanUtils::TransitionImageLayout(cmd, dst, dstFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        
        if (bp.dstName != RS::RENDER_OUTPUT) {
             m_ImageAccess[bp.dstName].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }

        // Restore SRC layout
        VulkanUtils::TransitionImageLayout(cmd, src.handle, src.format, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, srcAccess.layout);
    }

    VkFormat RenderGraph::GetImageFormat(std::string n) { return VK_FORMAT_UNDEFINED; }
    bool RenderGraph::ContainsImage(std::string n) { return m_Images.count(n); }
    const GraphImage& RenderGraph::GetImage(std::string n) const { return m_Images.at(n); }
    void RenderGraph::RegisterExternalResource(const std::string& n, const ImageDescription& d) {}
    void RenderGraph::SetExternalResource(const std::string& n, VkImage h, VkImageView v, VkImageLayout cl, VkAccessFlags ca, VkPipelineStageFlags cs) {}
    ImageAccess& RenderGraph::GetImageAccess(const std::string& n) { return m_ImageAccess[n]; }
    
    std::vector<std::string> RenderGraph::GetColorAttachments() const {
        std::vector<std::string> attachments;
        for (const auto& [name, img] : m_Images) {
            if (name != RS::RENDER_OUTPUT && name != RS::DEPTH) attachments.push_back(name);
        }
        return attachments;
    }

    void RenderGraph::DrawPerformanceStatistics() {
        ImGui::Text("RenderGraph Stats: Pass Count: %llu", m_Passes.size());
        ImGui::Text("Resource Count: %llu", m_Images.size());
    }
}