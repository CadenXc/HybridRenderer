#include "pch.h"
#include "RenderContext.h"
#include "VulkanContext.h"

namespace Chimera
{
    ScopedCommandBuffer::ScopedCommandBuffer()
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = VulkanContext::Get().GetCommandPool();
        allocInfo.commandBufferCount = 1;

        vkAllocateCommandBuffers(VulkanContext::Get().GetDevice(), &allocInfo, &m_CommandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(m_CommandBuffer, &beginInfo);
    }

    ScopedCommandBuffer::~ScopedCommandBuffer()
    {
        vkEndCommandBuffer(m_CommandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_CommandBuffer;

        vkQueueSubmit(VulkanContext::Get().GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(VulkanContext::Get().GetGraphicsQueue());

        vkFreeCommandBuffers(VulkanContext::Get().GetDevice(), VulkanContext::Get().GetCommandPool(), 1, &m_CommandBuffer);
    }
}
