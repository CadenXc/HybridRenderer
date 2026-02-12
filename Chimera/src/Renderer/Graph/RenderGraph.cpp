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

namespace Chimera
{
    VkImageView RenderGraphRegistry::GetImageView(RGResourceHandle h) { return graph.m_Resources[h].image.view; }
    VkImage RenderGraphRegistry::GetImage(RGResourceHandle h) { return graph.m_Resources[h].image.handle; }

    RGResourceHandle RenderGraph::PassBuilder::Read(const std::string& name) {
        RGResourceHandle h = graph.GetResourceHandle(name);
        pass.inputs.push_back({ h, ResourceUsage::GraphicsSampled });
        return h;
    }
    RGResourceHandle RenderGraph::PassBuilder::Write(const std::string& name, VkFormat format) {
        RGResourceHandle h = graph.GetResourceHandle(name);
        if (format != VK_FORMAT_UNDEFINED) graph.m_Resources[h].image.format = format;
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
    RGResourceHandle RenderGraph::PassBuilder::WriteStorage(const std::string& name, VkFormat format) {
        RGResourceHandle h = graph.GetResourceHandle(name);
        if (format != VK_FORMAT_UNDEFINED) graph.m_Resources[h].image.format = format;
        pass.outputs.push_back({ h, ResourceUsage::StorageWrite });
        return h;
    }

    RenderGraph::RenderGraph(VulkanContext& ctx, uint32_t w, uint32_t h) : m_Context(ctx), m_Width(w), m_Height(h) {
        VkQueryPoolCreateInfo info{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, nullptr, 0, VK_QUERY_TYPE_TIMESTAMP, 128 };
        vkCreateQueryPool(m_Context.GetDevice(), &info, nullptr, &m_TimestampQueryPool);
    }
    RenderGraph::~RenderGraph() { DestroyResources(true); if (m_TimestampQueryPool != VK_NULL_HANDLE) vkDestroyQueryPool(m_Context.GetDevice(), m_TimestampQueryPool, nullptr); }

    void RenderGraph::Reset() { m_PassStack.clear(); }

    void RenderGraph::Compile() {
        for (auto& pass : m_PassStack) {
            auto process = [&](const ResourceRequest& req) {
                auto& res = m_Resources[req.handle];
                
                // [FIX] 即使是 RENDER_OUTPUT，也要填充 pass 的格式信息，否则管线创建会失败
                VkFormat resFormat = res.image.format;
                if (res.name == RS::RENDER_OUTPUT) resFormat = m_Context.GetSwapChainImageFormat();

                if (res.name != RS::RENDER_OUTPUT && res.image.handle == VK_NULL_HANDLE && !res.isExternal) {
                    VkFormat format = (res.image.format != VK_FORMAT_UNDEFINED) ? res.image.format : VK_FORMAT_R16G16B16A16_SFLOAT;
                    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
                    if (VulkanUtils::IsDepthFormat(format)) usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                    else usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
                    res.image = ResourceManager::Get().CreateGraphImage(m_Width, m_Height, format, usage, VK_IMAGE_LAYOUT_UNDEFINED, VK_SAMPLE_COUNT_1_BIT);
                    resFormat = res.image.format;
                }

                if (req.usage == ResourceUsage::ColorAttachment) pass.colorFormats.push_back(resFormat);
                else if (req.usage == ResourceUsage::DepthStencilWrite) pass.depthFormat = resFormat;
            };
            for (auto& r : pass.inputs) process(r);
            for (auto& r : pass.outputs) process(r);
        }
    }

    void RenderGraph::Execute(VkCommandBuffer cmd) {
        auto swapchain = m_Context.GetSwapchain();
        uint32_t imageIndex = Renderer::Get().GetCurrentImageIndex();
        
        // [DEBUG] 检查交换链索引有效性
        if (imageIndex >= swapchain->GetImageViews().size()) {
            CH_CORE_ERROR("RenderGraph: Swapchain image index {0} out of bounds (size {1})!", imageIndex, swapchain->GetImageViews().size());
            return;
        }

        VkImage swapImage = swapchain->GetImages()[imageIndex];
        VkImageView swapView = swapchain->GetImageViews()[imageIndex];
        RGResourceHandle outHandle = GetResourceHandle(RS::RENDER_OUTPUT);
        
        m_Resources[outHandle].image.handle = swapImage;
        m_Resources[outHandle].image.view = swapView;
        m_Resources[outHandle].image.debug_view = swapView;
        m_Resources[outHandle].image.format = m_Context.GetSwapChainImageFormat();
        m_Resources[outHandle].isExternal = true; // [FIX] Mark as external so RenderGraph doesn't destroy swapchain views!
        
        if (m_Resources[outHandle].currentState.layout == VK_IMAGE_LAYOUT_UNDEFINED) {
            m_Resources[outHandle].currentState = { VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT };
        }

        for (auto& pass : m_PassStack) {
            BuildBarriers(cmd, pass);
            RenderGraphRegistry registry{ *this, pass };
            bool isGraphics = false; 
            std::vector<VkRenderingAttachmentInfo> colorAtts; 
            VkRenderingAttachmentInfo depthAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, nullptr }; 
            bool hasDepth = false;

            for (auto& out : pass.outputs) {
                if (out.usage == ResourceUsage::ColorAttachment || out.usage == ResourceUsage::DepthStencilWrite) {
                    isGraphics = true; 
                    auto& res = m_Resources[out.handle]; 
                    
                    VkImageView currentView = res.image.view;
                    
                    if (currentView == VK_NULL_HANDLE) {
                        CH_CORE_ERROR("RenderGraph: Pass '{0}' attempts to use NULL imageView for resource '{1}'!", pass.name, res.name);
                        isGraphics = false; // Disable graphics for this pass if view is missing
                        break;
                    }

                    if (out.usage == ResourceUsage::ColorAttachment) {
                        VkRenderingAttachmentInfo info{};
                        info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                        info.imageView = currentView;
                        info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                        info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                        info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                        info.clearValue = out.clearValue;
                        colorAtts.push_back(info);
                    } else { 
                        depthAtt = {};
                        depthAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                        depthAtt.imageView = currentView;
                        depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                        depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                        depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                        depthAtt.clearValue = out.clearValue;
                        hasDepth = true; 
                    }
                }
            }
            if (isGraphics && (!colorAtts.empty() || hasDepth)) {
                VkRenderingInfo renderingInfo{};
                renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                renderingInfo.renderArea = { {0, 0}, { m_Width, m_Height } };
                renderingInfo.layerCount = 1;
                renderingInfo.colorAttachmentCount = (uint32_t)colorAtts.size();
                renderingInfo.pColorAttachments = colorAtts.data();
                renderingInfo.pDepthAttachment = hasDepth ? &depthAtt : nullptr;

                vkCmdBeginRendering(cmd, &renderingInfo);
                VkViewport vp{ 0.0f, 0.0f, (float)m_Width, (float)m_Height, 0.0f, 1.0f }; 
                VkRect2D sc{ {0, 0}, { m_Width, m_Height } };
                vkCmdSetViewport(cmd, 0, 1, &vp); 
                vkCmdSetScissor(cmd, 0, 1, &sc);
            }
            if (pass.executeFunc) pass.executeFunc(registry, cmd);
            if (isGraphics && (!colorAtts.empty() || hasDepth)) vkCmdEndRendering(cmd);
        }

        for (auto& res : m_Resources) {
            if (res.name == RS::RENDER_OUTPUT) continue; 
            if (res.image.handle != VK_NULL_HANDLE && res.currentState.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                bool isDepth = VulkanUtils::IsDepthFormat(res.image.format);
                VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr, res.currentState.access, VK_ACCESS_SHADER_READ_BIT, res.currentState.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, res.image.handle, { (VkImageAspectFlags)(isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, 1, 0, 1 } };
                vkCmdPipelineBarrier(cmd, res.currentState.stage, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
                res.currentState = { VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT };
            }
        }
    }

    void RenderGraph::BuildBarriers(VkCommandBuffer cmd, RenderPass& pass) {
        auto process = [&](const ResourceRequest& req) {
            auto& res = m_Resources[req.handle]; if (res.image.handle == VK_NULL_HANDLE) return;
            VkImageLayout target; VkAccessFlags access; VkPipelineStageFlags stage;
            switch (req.usage) {
                case ResourceUsage::GraphicsSampled: target = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; access = VK_ACCESS_SHADER_READ_BIT; stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR; break;
                case ResourceUsage::ColorAttachment: target = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; break;
                case ResourceUsage::DepthStencilWrite: target = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT; break;
                case ResourceUsage::StorageWrite: target = VK_IMAGE_LAYOUT_GENERAL; access = VK_ACCESS_SHADER_WRITE_BIT; stage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT; break;
                default: return;
            }
            if (res.currentState.layout != target) {
                VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr, res.currentState.access, access, res.currentState.layout, target, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, res.image.handle, { (VkImageAspectFlags)(VulkanUtils::IsDepthFormat(res.image.format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, 1, 0, 1 } };
                vkCmdPipelineBarrier(cmd, res.currentState.stage, stage, 0, 0, nullptr, 0, nullptr, 1, &b);
                res.currentState = { target, access, stage };
            }
        };
        for (auto& r : pass.inputs) process(r);
        for (auto& r : pass.outputs) process(r);
    }

    RGResourceHandle RenderGraph::GetResourceHandle(const std::string& n) { if (m_ResourceMap.count(n)) return m_ResourceMap[n]; RGResourceHandle h = (RGResourceHandle)m_Resources.size(); m_Resources.push_back({ n }); m_ResourceMap[n] = h; return h; }
    void RenderGraph::DestroyResources(bool all) { uint32_t frameIdx = Renderer::HasInstance() ? Renderer::Get().GetCurrentFrameIndex() : 0; VkDevice device = m_Context.GetDevice(); VmaAllocator allocator = m_Context.GetAllocator(); for (auto& res : m_Resources) { if (!res.isExternal && res.image.handle != VK_NULL_HANDLE) { VkImage handle = res.image.handle; VkImageView view = res.image.view; VkImageView debugView = res.image.debug_view; VmaAllocation alloc = res.image.allocation; auto destroyFunc = [device, allocator, handle, view, debugView, alloc]() { if (view != VK_NULL_HANDLE) vkDestroyImageView(device, view, nullptr); if (debugView != VK_NULL_HANDLE && debugView != view) vkDestroyImageView(device, debugView, nullptr); if (alloc != nullptr) vmaDestroyImage(allocator, handle, alloc); }; if (all) destroyFunc(); else if (Renderer::HasInstance()) m_Context.GetDeletionQueue().PushFunction(frameIdx, destroyFunc); else destroyFunc(); res.image.handle = VK_NULL_HANDLE; } } if (all) { m_Resources.clear(); m_ResourceMap.clear(); } }
    bool RenderGraph::ContainsImage(const std::string& n) { return m_ResourceMap.count(n) > 0; }
    const GraphImage& RenderGraph::GetImage(const std::string& n) const { if (m_ResourceMap.count(n)) return m_Resources[m_ResourceMap.at(n)].image; static GraphImage d{}; return d; }
    
    std::vector<std::string> RenderGraph::GetColorAttachments() const { 
        std::vector<std::string> result; 
        for (const auto& res : m_Resources) {
            // [FIX] 只要有 handle 就显示，不再过滤深度，方便调试
            if (res.image.handle != VK_NULL_HANDLE) result.push_back(res.name); 
        }
        return result; 
    }
    void RenderGraph::DrawPerformanceStatistics() { ImGui::Begin("RenderGraph"); ImGui::Text("Passes: %d", (int)m_PassStack.size()); ImGui::End(); }
}
