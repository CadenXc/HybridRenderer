#pragma once

#include "pch.h"
#include "Renderer/Backend/VulkanContext.h"

namespace Chimera
{
    // Frame resource structure forward declaration
    struct FrameResource;

    class Renderer
    {
    public:
        // Constants
        static constexpr uint32_t MaxFramesInFlight = 3;

        Renderer();
        ~Renderer();

        // Disable copying
        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        static Renderer& Get() { return *s_Instance; }
        static bool HasInstance() { return s_Instance != nullptr; }

        // Core rendering loop: begin frame
        // Returns command buffer for recording. Returns VK_NULL_HANDLE if swapchain is out of date
        VkCommandBuffer BeginFrame();

        // Core rendering loop: end frame
        // Submits command buffer and requests present
        void EndFrame();

        // Resets internal frame state in case of exception
        void ResetFrameState()
        {
            m_IsFrameInProgress = false;
            m_ActiveCommandBuffer = VK_NULL_HANDLE;
        }

        // Called when window is resized
        void OnResize(uint32_t width, uint32_t height);

        // Getters
        uint32_t GetCurrentFrameIndex() const
        {
            return m_CurrentFrameIndex;
        }

        uint32_t GetCurrentImageIndex() const
        {
            return m_CurrentImageIndex;
        }

        VkCommandBuffer GetActiveCommandBuffer() const
        {
            return m_ActiveCommandBuffer;
        }

        bool IsFrameInProgress() const
        {
            return m_IsFrameInProgress;
        }

        void SetComputeWaitSemaphore(VkSemaphore sem) { m_ComputeWaitSemaphore = sem; }

    private:
        void CreateFrameResources();
        void FreeFrameResources();
        void RecreateSwapchain();

    private:
        static Renderer* s_Instance;
        VkCommandPool m_CommandPool = VK_NULL_HANDLE;
        std::vector<FrameResource> m_FrameResources;
        VkCommandBuffer m_ActiveCommandBuffer = VK_NULL_HANDLE;
        VkSemaphore m_ComputeWaitSemaphore = VK_NULL_HANDLE;

        uint32_t m_CurrentFrameIndex = 0;
        uint32_t m_CurrentImageIndex = 0;
        
        bool m_IsFrameInProgress = false;
        bool m_NeedResize = false;
    };
}