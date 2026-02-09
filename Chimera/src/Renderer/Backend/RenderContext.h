#pragma once
#include "pch.h"
#include "VulkanContext.h"

namespace Chimera
{
    /**
     * @brief A simple wrapper for managing Vulkan command buffers, 
     * particularly useful for one-time setup/transfer commands.
     */
    struct ScopedCommandBuffer
    {
        ScopedCommandBuffer();
        ~ScopedCommandBuffer();

        operator VkCommandBuffer() const
        {
            return m_CommandBuffer;
        }

    private:
        VkCommandBuffer m_CommandBuffer;
    };

    /**
     * @brief RenderContext provides a centralized interface for common 
     * rendering operations and resource state management.
     */
    class RenderContext
    {
    public:
        static void Init();
        static void Shutdown();

        // One-time command helpers
        static VkCommandBuffer BeginSingleTimeCommands();
        static void EndSingleTimeCommands(VkCommandBuffer commandBuffer);
    };
}