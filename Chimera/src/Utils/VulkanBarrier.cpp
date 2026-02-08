#include "pch.h"
#include "Utils/VulkanBarrier.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Backend/RenderContext.h"

namespace Chimera::VulkanUtils
{
    VkImageMemoryBarrier CreateImageBarrier(
        VkImage image,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        VkAccessFlags srcAccess,
        VkAccessFlags dstAccess,
        VkImageAspectFlags aspectMask
    )
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = dstAccess;
        barrier.subresourceRange.aspectMask = aspectMask;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        return barrier;
    }

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
    )
    {
        if (srcStageMask == 0) srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        if (dstStageMask == 0) dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

        VkImageMemoryBarrier barrier = CreateImageBarrier(image, oldImageLayout, newImageLayout, srcAccessMask, dstAccessMask, aspectMask);
        vkCmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    bool IsDepthFormat(VkFormat format)
    {
        static const std::vector<VkFormat> depthFormats = {
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D24_UNORM_S8_UINT,
            VK_FORMAT_D16_UNORM_S8_UINT,
            VK_FORMAT_D16_UNORM
        };
        return std::find(depthFormats.begin(), depthFormats.end(), format) != depthFormats.end();
    }

    bool IsSRGBFormat(VkFormat format)
    {
        static const std::vector<VkFormat> srgbFormats = {
            VK_FORMAT_R8G8B8A8_SRGB,
            VK_FORMAT_B8G8R8A8_SRGB,
            VK_FORMAT_R8G8B8_SRGB,
            VK_FORMAT_B8G8R8_SRGB
        };
        return std::find(srgbFormats.begin(), srgbFormats.end(), format) != srgbFormats.end();
    }

    void TransitionImageLayout(
        VkCommandBuffer commandBuffer,
        VkImage image,
        VkFormat format,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        uint32_t mipLevels)
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL || IsDepthFormat(format)) 
        {
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            if (format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT) 
            {
                barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            }
        } 
        else 
        {
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        }

        VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

        switch (oldLayout) {
            case VK_IMAGE_LAYOUT_UNDEFINED: barrier.srcAccessMask = 0; sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; break;
            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; break;
            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; sourceStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT; break;
            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT; break;
            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT; break;
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; break;
            case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: barrier.srcAccessMask = 0; sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; break;
            case VK_IMAGE_LAYOUT_GENERAL: barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT; sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT; break;
            default: barrier.srcAccessMask = 0; sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; break;
        }

        switch (newLayout) {
            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT; break;
            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT; destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT; break;
            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; break;
            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: barrier.dstAccessMask = barrier.dstAccessMask | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT; break;
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                if (barrier.srcAccessMask == 0) barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                break;
            case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: barrier.dstAccessMask = 0; destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT; break;
            case VK_IMAGE_LAYOUT_GENERAL: barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT; destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT; break;
            default: barrier.dstAccessMask = 0; destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT; break;
        }

        vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    void TransitionImageLayout(
        std::shared_ptr<VulkanContext> context,
        VkImage image,
        VkFormat format,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        uint32_t mipLevels)
    {
        ScopedCommandBuffer cmd(context);
        TransitionImageLayout(cmd, image, format, oldLayout, newLayout, mipLevels);
    }

    VkImageLayout GetImageLayoutFromResourceType(TransientResourceType type, VkFormat format)
    {
        if (IsDepthFormat(format)) return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        
        switch (type) {
            case TransientResourceType::Sampler: return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            case TransientResourceType::Storage: return VK_IMAGE_LAYOUT_GENERAL;
            case TransientResourceType::Image:   return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            default: return VK_IMAGE_LAYOUT_GENERAL;
        }
    }
}