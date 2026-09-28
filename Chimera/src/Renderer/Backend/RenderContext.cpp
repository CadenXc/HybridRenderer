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
    m_Pool = context.GetThreadLocalCommandPool();

    VkCommandBufferAllocateInfo allocInfo{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_Pool;
    allocInfo.commandBufferCount = 1;

    VK_CHECK(vkAllocateCommandBuffers(m_Device, &allocInfo, &m_CommandBuffer));

    VkCommandBufferBeginInfo beginInfo{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
    VkResult beginResult = vkBeginCommandBuffer(m_CommandBuffer, &beginInfo);
    if (beginResult != VK_SUCCESS)
    {
        FreeCommandBuffer();
        VK_CHECK(beginResult);
    }
}

ScopedCommandBuffer::~ScopedCommandBuffer() noexcept
{
    if (m_CommandBuffer != VK_NULL_HANDLE)
    {
        CH_CORE_WARN(
            "ScopedCommandBuffer: Discarding a command buffer that was not submitted");
        FreeCommandBuffer();
    }
}

void ScopedCommandBuffer::SubmitAndWait()
{
    if (m_CommandBuffer == VK_NULL_HANDLE)
    {
        throw std::logic_error(
            "ScopedCommandBuffer::SubmitAndWait called without an active command buffer");
    }

    VkResult endResult = vkEndCommandBuffer(m_CommandBuffer);
    if (endResult != VK_SUCCESS)
    {
        FreeCommandBuffer();
        VK_CHECK(endResult);
    }

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_CommandBuffer;

    VkResult submitResult = VK_SUCCESS;
    VkResult waitResult = VK_SUCCESS;
    {
        std::lock_guard<std::mutex> lock(VulkanContext::GetGlobalQueueMutex());
        submitResult =
            vkQueueSubmit(m_Queue, 1, &submitInfo, VK_NULL_HANDLE);
        if (submitResult == VK_SUCCESS)
        {
            waitResult = vkQueueWaitIdle(m_Queue);
        }
    }

    if (submitResult != VK_SUCCESS)
    {
        FreeCommandBuffer();
        VK_CHECK(submitResult);
    }

    if (waitResult != VK_SUCCESS)
    {
        // Queue completion is unknown after a failed wait. The command pool
        // will release this buffer when the Vulkan context is destroyed.
        m_CommandBuffer = VK_NULL_HANDLE;
        VK_CHECK(waitResult);
    }

    FreeCommandBuffer();
}

void ScopedCommandBuffer::FreeCommandBuffer() noexcept
{
    if (m_CommandBuffer == VK_NULL_HANDLE) return;

    vkFreeCommandBuffers(m_Device, m_Pool, 1, &m_CommandBuffer);
    m_CommandBuffer = VK_NULL_HANDLE;
}
} // namespace Chimera
