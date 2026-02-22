#pragma once
#include "pch.h"
#include "Renderer/ChimeraCommon.h"
#include "Renderer/Graph/RenderGraphCommon.h"

namespace Chimera
{
    class VulkanContext;
    class Buffer;

    namespace VulkanUtils
    {
        struct RaytracingPipeline; // Forward declaration

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

        // [NEW] Synchronization 2 transition
        void TransitionImage(
            VkCommandBuffer commandBuffer,
            VkImage image,
            VkImageLayout oldLayout,
            VkImageLayout newLayout,
            VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            uint32_t mipLevels = 1
        );

        // 直接录制到 CommandBuffer (用于 Render Loop) - Legacy wrapper
        void TransitionImageLayout(
            VkCommandBuffer commandBuffer,
            VkImage image,
            VkFormat format,
            VkImageLayout oldLayout,
            VkImageLayout newLayout,
            uint32_t mipLevels = 1
        );

        // 自动创建临时 CommandBuffer 并提交 (用于初始化)
        void TransitionImageLayout(
            VkImage image,
            VkFormat format,
            VkImageLayout oldLayout,
            VkImageLayout newLayout,
            uint32_t mipLevels = 1
        );

        bool IsDepthFormat(VkFormat format);
        bool IsSRGBFormat(VkFormat format);

        uint32_t AlignUp(uint32_t value, uint32_t alignment);

        // Uses internal struct from PipelineManager
        std::unique_ptr<Buffer> CreateSBT(VkPipeline pipeline, uint32_t raygenCount, uint32_t missCount, uint32_t hitCount, VkStridedDeviceAddressRegionKHR& outRaygen, VkStridedDeviceAddressRegionKHR& outMiss, VkStridedDeviceAddressRegionKHR& outHit);
    }
}