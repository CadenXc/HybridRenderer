#pragma once
#include "pch.h"
#include "gfx/vulkan/VulkanCommon.h"

namespace Chimera {
    class VulkanContext;

    namespace VulkanUtils {

        VkImageMemoryBarrier CreateImageBarrier(
            VkImage image,
            VkImageLayout oldLayout,
            VkImageLayout newLayout,
            VkAccessFlags srcAccess,
            VkAccessFlags dstAccess,
            VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT
        );

        void InsertImageBarrier(
            VkCommandBuffer commandBuffer, 
            VkImage image, 
            VkImageAspectFlags aspectMask, 
            VkImageLayout oldImageLayout, 
            VkImageLayout newImageLayout, 
            VkPipelineStageFlags srcStageMask, 
            VkPipelineStageFlags dstStageMask, 
            VkAccessFlags srcAccessMask, 
            VkAccessFlags dstAccessMask
        );

        // [新增] 核心实现：直接录制到 CommandBuffer (用于 Render Loop)
        void TransitionImageLayout(
            VkCommandBuffer commandBuffer,
            VkImage image,
            VkFormat format,
            VkImageLayout oldLayout,
            VkImageLayout newLayout,
            uint32_t mipLevels = 1
        );

        // [原有] 辅助封装：自动创建临时 CommandBuffer 并提交 (用于初始化)
        void TransitionImageLayout(
            std::shared_ptr<VulkanContext> context,
            VkImage image,
            VkFormat format,
            VkImageLayout oldLayout,
            VkImageLayout newLayout,
            uint32_t mipLevels = 1
        );

        bool IsDepthFormat(VkFormat format);
        VkImageLayout GetImageLayoutFromResourceType(TransientImageType type, VkFormat format);
    }
}