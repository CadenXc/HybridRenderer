#include "pch.h"
#include "Utils/VulkanBarrier.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Backend/RenderContext.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/Resources/Buffer.h"

namespace Chimera::VulkanUtils
{
VkImageMemoryBarrier CreateImageBarrier(VkImage image, VkImageLayout oldLayout,
                                        VkImageLayout newLayout,
                                        VkAccessFlags srcAccess,
                                        VkAccessFlags dstAccess,
                                        VkImageAspectFlags aspectMask)
{
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspectMask;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    return barrier;
}

void TransitionImage(VkCommandBuffer commandBuffer, VkImage image,
                     VkImageLayout oldLayout, VkImageLayout newLayout,
                     VkImageAspectFlags aspectMask, uint32_t mipLevels)
{
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.image = image;
    barrier.subresourceRange = {aspectMask, 0, mipLevels, 0, 1};
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    switch (oldLayout)
    {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            barrier.srcAccessMask = 0;
            break;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            barrier.srcStageMask =
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            barrier.srcStageMask =
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            barrier.srcAccessMask =
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            barrier.srcAccessMask = 0;
            break;
        case VK_IMAGE_LAYOUT_GENERAL:
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier.srcAccessMask =
                VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
            break;
        default:
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier.srcAccessMask =
                VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
            break;
    }

    switch (newLayout)
    {
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            barrier.dstStageMask =
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                                    VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            barrier.dstStageMask =
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            barrier.dstAccessMask =
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            barrier.dstStageMask =
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
            barrier.dstAccessMask = 0;
            break;
        case VK_IMAGE_LAYOUT_GENERAL:
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier.dstAccessMask =
                VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
            break;
        default:
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier.dstAccessMask =
                VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
            break;
    }

    VkDependencyInfo dependencyInfo{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

void TransitionImageLayout(VkCommandBuffer cmd, VkImage img, VkFormat fmt,
                           VkImageLayout oldL, VkImageLayout newL, uint32_t mip)
{
    VkImageAspectFlags aspect = IsDepthFormat(fmt) ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                   : VK_IMAGE_ASPECT_COLOR_BIT;
    TransitionImage(cmd, img, oldL, newL, aspect, mip);
}

void TransitionImageLayout(VkImage img, VkFormat fmt, VkImageLayout oldL,
                           VkImageLayout newL, uint32_t mip)
{
    ScopedCommandBuffer cmd;
    TransitionImageLayout(cmd, img, fmt, oldL, newL, mip);
}

bool IsDepthFormat(VkFormat format)
{
    static const std::vector<VkFormat> depthFormats = {
        VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM};
    return std::find(depthFormats.begin(), depthFormats.end(), format) !=
           depthFormats.end();
}

bool IsSRGBFormat(VkFormat format)
{
    static const std::vector<VkFormat> srgbFormats = {
        VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8_SRGB, VK_FORMAT_R8G8B8_SRGB,
        VK_FORMAT_B8G8R8_SRGB};
    return std::find(srgbFormats.begin(), srgbFormats.end(), format) !=
           srgbFormats.end();
}

uint32_t AlignUp(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

std::unique_ptr<Buffer> CreateSBT(VkPipeline pipeline, uint32_t raygenCount,
                                  uint32_t missCount, uint32_t hitCount,
                                  VkStridedDeviceAddressRegionKHR& outRaygen,
                                  VkStridedDeviceAddressRegionKHR& outMiss,
                                  VkStridedDeviceAddressRegionKHR& outHit)
{
    if (raygenCount != 1)
    {
        throw std::runtime_error(
            "CreateSBT requires exactly one ray generation record");
    }

    outRaygen = {};
    outMiss = {};
    outHit = {};

    VkDevice device = VulkanContext::Get().GetDevice();
    auto rayTracingProperties = VulkanContext::Get().GetRayTracingProperties();

    const uint32_t handleSize = rayTracingProperties.shaderGroupHandleSize;
    const uint32_t recordStride =
        AlignUp(handleSize, rayTracingProperties.shaderGroupHandleAlignment);
    const uint32_t baseAlignment =
        rayTracingProperties.shaderGroupBaseAlignment;

    const uint32_t groupCount = raygenCount + missCount + hitCount;
    const uint32_t handleStorageSize = groupCount * handleSize;

    std::vector<uint8_t> shaderHandles(handleStorageSize);
    VK_CHECK(vkGetRayTracingShaderGroupHandlesKHR(
        device, pipeline, 0, groupCount, handleStorageSize,
        shaderHandles.data()));

    const uint32_t raygenDataSize = raygenCount * recordStride;
    const uint32_t missOffset = AlignUp(raygenDataSize, baseAlignment);
    const uint32_t missDataSize = missCount * recordStride;
    const uint32_t hitOffset =
        AlignUp(missOffset + missDataSize, baseAlignment);
    const uint32_t hitDataSize = hitCount * recordStride;
    const uint32_t totalSBTSize = hitOffset + hitDataSize;

    std::vector<uint8_t> sbtData(totalSBTSize, 0);
    auto copyRecords = [&](uint32_t destinationOffset, uint32_t sourceGroup,
                           uint32_t count)
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            memcpy(sbtData.data() + destinationOffset + i * recordStride,
                   shaderHandles.data() + (sourceGroup + i) * handleSize,
                   handleSize);
        }
    };

    copyRecords(0, 0, raygenCount);
    copyRecords(missOffset, raygenCount, missCount);
    copyRecords(hitOffset, raygenCount + missCount, hitCount);

    auto sbtBuffer =
        std::make_unique<Buffer>(totalSBTSize,
                                 VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
                                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                 VMA_MEMORY_USAGE_CPU_TO_GPU,
                                 "ShaderBindingTable");

    const VkDeviceAddress baseAddr = sbtBuffer->GetDeviceAddress();
    if (baseAddr % baseAlignment != 0)
    {
        throw std::runtime_error(
            "SBT buffer device address does not satisfy base alignment");
    }

    sbtBuffer->Update(sbtData.data(), totalSBTSize);

    outRaygen.deviceAddress = baseAddr;
    outRaygen.stride = recordStride;
    outRaygen.size = recordStride;

    if (missCount > 0)
    {
        outMiss.deviceAddress = baseAddr + missOffset;
        outMiss.stride = recordStride;
        outMiss.size = missDataSize;
    }

    if (hitCount > 0)
    {
        outHit.deviceAddress = baseAddr + hitOffset;
        outHit.stride = recordStride;
        outHit.size = hitDataSize;
    }

    return sbtBuffer;
}
} // namespace Chimera::VulkanUtils
