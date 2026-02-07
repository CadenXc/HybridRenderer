#pragma once
#include "pch.h"
#include "VulkanContext.h"

namespace Chimera {

    /**
     * @brief A simple wrapper for managing Vulkan command buffers, 
     * particularly useful for one-time setup/transfer commands.
     */
    struct ScopedCommandBuffer {
        ScopedCommandBuffer(const std::shared_ptr<VulkanContext>& context);
        ~ScopedCommandBuffer();

        operator VkCommandBuffer() const { return m_CommandBuffer; }

    private:
        std::shared_ptr<VulkanContext> m_Context;
        VkCommandBuffer m_CommandBuffer;
    };

    /**
     * @brief RenderContext provides a centralized interface for common 
     * rendering operations and resource state management.
     */
    class RenderContext {
    public:
        static void Init(const std::shared_ptr<VulkanContext>& context);
        static void Shutdown();

        // One-time command helpers
        static VkCommandBuffer BeginSingleTimeCommands();
        static void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

    private:
        inline static std::shared_ptr<VulkanContext> s_Context = nullptr;
    };

}
