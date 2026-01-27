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