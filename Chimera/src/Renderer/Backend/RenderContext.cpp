#include "pch.h"
#include "RenderContext.h"

namespace Chimera {

    // --- ScopedCommandBuffer ---

    ScopedCommandBuffer::ScopedCommandBuffer(const std::shared_ptr<VulkanContext>& context)
        : m_Context(context)
    {
        m_CommandBuffer = RenderContext::BeginSingleTimeCommands();
    }

    ScopedCommandBuffer::~ScopedCommandBuffer()
    {
        RenderContext::EndSingleTimeCommands(m_CommandBuffer);
    }

    // --- RenderContext ---

    void RenderContext::Init(const std::shared_ptr<VulkanContext>& context) {
        s_Context = context;
    }

    void RenderContext::Shutdown() {
        s_Context.reset();
    }

    VkCommandBuffer RenderContext::BeginSingleTimeCommands() {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = s_Context->GetCommandPool();
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(s_Context->GetDevice(), &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        return commandBuffer;
    }

    void RenderContext::EndSingleTimeCommands(VkCommandBuffer commandBuffer) {
        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        vkQueueSubmit(s_Context->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(s_Context->GetGraphicsQueue());

        vkFreeCommandBuffers(s_Context->GetDevice(), s_Context->GetCommandPool(), 1, &commandBuffer);
    }

}
