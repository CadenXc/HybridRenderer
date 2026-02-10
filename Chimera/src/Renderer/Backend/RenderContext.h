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
}