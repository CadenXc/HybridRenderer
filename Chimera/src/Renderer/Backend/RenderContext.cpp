#include "pch.h"
#include "RenderContext.h"
#include "VulkanContext.h"

namespace Chimera
{
    ScopedCommandBuffer::ScopedCommandBuffer()
    {
        auto& context = VulkanContext::Get();
        m_Device = context.GetDevice();
        m_Queue = context.GetGraphicsQueue();
        m_Pool = context.GetCommandPool();

        VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = m_Pool;
        allocInfo.commandBufferCount = 1;

        vkAllocateCommandBuffers(m_Device, &allocInfo, &m_CommandBuffer);

        VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(m_CommandBuffer, &beginInfo);
    }

    ScopedCommandBuffer::~ScopedCommandBuffer()
    {
        if (m_CommandBuffer == VK_NULL_HANDLE) return;

        vkEndCommandBuffer(m_CommandBuffer);

        VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_CommandBuffer;

        vkQueueSubmit(m_Queue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_Queue);

        vkFreeCommandBuffers(m_Device, m_Pool, 1, &m_CommandBuffer);
    }
}