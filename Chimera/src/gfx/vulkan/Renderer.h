#pragma once

#include "pch.h"
#include "gfx/vulkan/VulkanContext.h"

namespace Chimera {

    // Frame resource structure forward declaration
    struct FrameResource;

    class Renderer {
    public:
        // Constants
        static constexpr uint32_t MaxFramesInFlight = 3;

        Renderer(const std::shared_ptr<VulkanContext>& context);
        ~Renderer();

        // Disable copying
        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        // Core rendering loop: begin frame
        // Returns command buffer for recording. Returns VK_NULL_HANDLE if swapchain is out of date
        VkCommandBuffer BeginFrame();

        // Core rendering loop: end frame
        // Submits command buffer and requests present
        void EndFrame();

        // Called when window is resized
        void OnResize(uint32_t width, uint32_t height);

        // Getters
        uint32_t GetCurrentFrameIndex() const { return m_CurrentFrameIndex; }
        uint32_t GetCurrentImageIndex() const { return m_CurrentImageIndex; }
        VkRenderPass GetSwapchainRenderPass() const { return m_SwapchainRenderPass; }

        bool IsFrameInProgress() const { return m_IsFrameInProgress; }

    private:
        void CreateFrameResources();
        void FreeFrameResources();
        void RecreateSwapchain();

    private:
        std::shared_ptr<VulkanContext> m_Context;
        std::vector<FrameResource> m_FrameResources;

        uint32_t m_CurrentFrameIndex = 0;
        uint32_t m_CurrentImageIndex = 0;
        
        bool m_IsFrameInProgress = false;
        bool m_NeedResize = false;

        VkRenderPass m_SwapchainRenderPass = VK_NULL_HANDLE;
    };

}

