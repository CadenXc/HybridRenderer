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
        VkDevice m_Device = VK_NULL_HANDLE;
        VkQueue m_Queue = VK_NULL_HANDLE;
        VkCommandPool m_Pool = VK_NULL_HANDLE;
        VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
    };
}