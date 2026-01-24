#pragma once

#include "VulkanContext.h"
#include <vector>
#include <memory>

namespace Chimera {

    class Renderer
    {
    public:
        Renderer(const std::shared_ptr<VulkanContext>& context);
        ~Renderer();

        VkCommandBuffer BeginFrame();
        void EndFrame();

        uint32_t GetCurrentFrameIndex() const { return m_CurrentFrameIndex; }
        uint32_t GetCurrentImageIndex() const { return m_CurrentImageIndex; }
        bool IsFrameInProgress() const { return m_IsFrameStarted; }

        VkCommandBuffer GetCurrentCommandBuffer() const
        {
            assert(m_IsFrameStarted && "Cannot get command buffer when frame not in progress");
            return m_CommandBuffers[m_CurrentFrameIndex];
        }

    private:
        void CreateCommandBuffers();
        void FreeCommandBuffers();
        void RecreateSwapChain();

    private:
        std::shared_ptr<VulkanContext> m_Context;

        uint32_t m_CurrentFrameIndex = 0;
        uint32_t m_CurrentImageIndex = 0; // Index of the swapchain image we are currently rendering to
        bool m_IsFrameStarted = false;

        std::vector<VkCommandBuffer> m_CommandBuffers;

        // Sync Objects
        std::vector<VkSemaphore> m_ImageAvailableSemaphores;
        std::vector<VkSemaphore> m_RenderFinishedSemaphores;
        std::vector<VkFence> m_InFlightFences;
        // Keep track of fences for each swapchain image to handle resizing correctly
        std::vector<VkFence> m_ImagesInFlight; 
    };

}
