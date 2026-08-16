#include "pch.h"
#include "Renderer/Backend/Renderer.h"
#include "Renderer/Resources/Buffer.h"
#include "Renderer/Backend/RenderContext.h"
#include "Renderer/Resources/ResourceManager.h"

#include <stb_image_write.h>

namespace Chimera
{
Renderer* Renderer::s_Instance = nullptr;

struct FrameResource
{
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence inFlightFence = VK_NULL_HANDLE;

    std::unique_ptr<Buffer> captureBuffer;
    std::filesystem::path capturePath;
    uint32_t captureWidth = 0;
    uint32_t captureHeight = 0;
    VkFormat captureFormat = VK_FORMAT_UNDEFINED;
    bool capturePending = false;
};

Renderer::Renderer()
{
    s_Instance = this;
    CreateFrameResources();
}

Renderer::~Renderer()
{
    vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
    FreeFrameResources();
    s_Instance = nullptr;
}

void Renderer::CreateFrameResources()
{
    VkDevice device = VulkanContext::Get().GetDevice();

    VkCommandPoolCreateInfo poolInfo{
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = VulkanContext::Get().GetGraphicsQueueFamily();

    VK_CHECK(vkCreateCommandPool(device, &poolInfo, nullptr, &m_CommandPool));

    m_FrameResources.resize(MaxFramesInFlight);

    for (int i = 0; i < MaxFramesInFlight; ++i)
    {
        VkCommandBufferAllocateInfo allocInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocInfo.commandPool = m_CommandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo,
                                          &m_FrameResources[i].commandBuffer));

        VkSemaphoreCreateInfo semaphoreInfo{
            VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(
            vkCreateSemaphore(device, &semaphoreInfo, nullptr,
                              &m_FrameResources[i].imageAvailableSemaphore));
        VK_CHECK(
            vkCreateSemaphore(device, &semaphoreInfo, nullptr,
                              &m_FrameResources[i].renderFinishedSemaphore));

        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr,
                               &m_FrameResources[i].inFlightFence));
    }
}

void Renderer::FreeFrameResources()
{
    VkDevice device = VulkanContext::Get().GetDevice();

    for (auto& frameResource : m_FrameResources)
    {
        if (frameResource.inFlightFence != VK_NULL_HANDLE)
        {
            vkDestroyFence(device, frameResource.inFlightFence, nullptr);
        }
        if (frameResource.renderFinishedSemaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device, frameResource.renderFinishedSemaphore,
                               nullptr);
        }
        if (frameResource.imageAvailableSemaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device, frameResource.imageAvailableSemaphore,
                               nullptr);
        }
    }

    if (m_CommandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(device, m_CommandPool, nullptr);
    }

    m_FrameResources.clear();
}

void Renderer::OnResize(uint32_t width, uint32_t height)
{
    m_NeedResize = true;
}

bool Renderer::RequestFrameCapture(
    const std::filesystem::path& outputPath)
{
    if (outputPath.empty() || m_FrameCaptureRequested)
    {
        return false;
    }

    m_FrameCapturePath = outputPath;
    m_FrameCaptureRequested = true;
    return true;
}

void Renderer::RecordFrameCapture(VkCommandBuffer commandBuffer)
{
    if (!m_FrameCaptureRequested || commandBuffer == VK_NULL_HANDLE)
    {
        return;
    }

    VkExtent2D extent = VulkanContext::Get().GetSwapChainExtent();
    VkFormat format = VulkanContext::Get().GetSwapChainImageFormat();

    const bool supportedFormat = format == VK_FORMAT_B8G8R8A8_SRGB ||
                                 format == VK_FORMAT_B8G8R8A8_UNORM ||
                                 format == VK_FORMAT_R8G8B8A8_SRGB ||
                                 format == VK_FORMAT_R8G8B8A8_UNORM;

    if (!supportedFormat)
    {
        CH_CORE_ERROR("Frame capture does not support swapchain format {}",
                      static_cast<int>(format));

        m_FrameCaptureRequested = false;
        m_FrameCapturePath.clear();
        return;
    }

    const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(extent.width) *
                                    static_cast<VkDeviceSize>(extent.height) *
                                    4;

    FrameResource& frameResource = m_FrameResources[m_CurrentFrameIndex];

    frameResource.captureBuffer = std::make_unique<Buffer>(
        bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_TO_CPU, "FrameCaptureReadback");

    frameResource.capturePath = m_FrameCapturePath;
    frameResource.captureWidth = extent.width;
    frameResource.captureHeight = extent.height;
    frameResource.captureFormat = format;

    VkImage sourceImage =
        VulkanContext::Get().GetSwapChainImages()[m_CurrentImageIndex];

    VkImageMemoryBarrier2 beforeCopy{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};

    beforeCopy.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    beforeCopy.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    beforeCopy.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    beforeCopy.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    beforeCopy.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    beforeCopy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    beforeCopy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    beforeCopy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    beforeCopy.image = sourceImage;
    beforeCopy.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo beforeCopyDependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};

    beforeCopyDependency.imageMemoryBarrierCount = 1;
    beforeCopyDependency.pImageMemoryBarriers = &beforeCopy;

    vkCmdPipelineBarrier2(commandBuffer, &beforeCopyDependency);

    VkBufferImageCopy copyRegion{};
    copyRegion.bufferOffset = 0;
    copyRegion.bufferRowLength = 0;
    copyRegion.bufferImageHeight = 0;
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.mipLevel = 0;
    copyRegion.imageSubresource.baseArrayLayer = 0;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageOffset = {0, 0, 0};
    copyRegion.imageExtent = {extent.width, extent.height, 1};

    vkCmdCopyImageToBuffer(
        commandBuffer, sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        frameResource.captureBuffer->GetBuffer(), 1, &copyRegion);

    VkImageMemoryBarrier2 afterCopy{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};

    afterCopy.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    afterCopy.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    afterCopy.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    afterCopy.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                              VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    afterCopy.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    afterCopy.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    afterCopy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    afterCopy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    afterCopy.image = sourceImage;
    afterCopy.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo afterCopyDependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};

    afterCopyDependency.imageMemoryBarrierCount = 1;
    afterCopyDependency.pImageMemoryBarriers = &afterCopy;

    vkCmdPipelineBarrier2(commandBuffer, &afterCopyDependency);

    frameResource.capturePending = true;

    m_FrameCaptureRequested = false;
    m_FrameCapturePath.clear();
}

void Renderer::ProcessCompletedFrameCapture(
    FrameResource& frameResource)
{
    if (!frameResource.capturePending ||
        !frameResource.captureBuffer)
    {
        return;
    }

    const size_t pixelCount =
        static_cast<size_t>(frameResource.captureWidth) *
        static_cast<size_t>(frameResource.captureHeight);

    const size_t byteCount = pixelCount * 4;

    const uint8_t* mappedPixels =
        static_cast<const uint8_t*>(
            frameResource.captureBuffer->Map());

    if (!mappedPixels)
    {
        CH_CORE_ERROR("Frame capture failed to map readback buffer");

        frameResource.captureBuffer.reset();
        frameResource.capturePath.clear();
        frameResource.capturePending = false;
        return;
    }

    frameResource.captureBuffer->Invalidate(
        static_cast<VkDeviceSize>(byteCount));

    std::vector<uint8_t> rgbaPixels(byteCount);

    const bool sourceIsBGRA =
        frameResource.captureFormat ==
            VK_FORMAT_B8G8R8A8_SRGB ||
        frameResource.captureFormat ==
            VK_FORMAT_B8G8R8A8_UNORM;

    if (sourceIsBGRA)
    {
        for (size_t pixelIndex = 0;
             pixelIndex < pixelCount;
             ++pixelIndex)
        {
            const size_t offset = pixelIndex * 4;

            rgbaPixels[offset + 0] =
                mappedPixels[offset + 2];
            rgbaPixels[offset + 1] =
                mappedPixels[offset + 1];
            rgbaPixels[offset + 2] =
                mappedPixels[offset + 0];
            rgbaPixels[offset + 3] =
                mappedPixels[offset + 3];
        }
    }
    else
    {
        std::memcpy(
            rgbaPixels.data(),
            mappedPixels,
            byteCount);
    }

    frameResource.captureBuffer->Unmap();

    const std::filesystem::path parentPath =
        frameResource.capturePath.parent_path();

    if (!parentPath.empty())
    {
        std::error_code directoryError;
        std::filesystem::create_directories(
            parentPath, directoryError);

        if (directoryError)
        {
            CH_CORE_ERROR(
                "Frame capture failed to create directory: {}",
                directoryError.message());

            frameResource.captureBuffer.reset();
            frameResource.capturePath.clear();
            frameResource.capturePending = false;
            return;
        }
    }

    const std::string outputPath =
        frameResource.capturePath.string();

    const int writeSucceeded = stbi_write_png(
        outputPath.c_str(),
        static_cast<int>(frameResource.captureWidth),
        static_cast<int>(frameResource.captureHeight),
        4,
        rgbaPixels.data(),
        static_cast<int>(
            frameResource.captureWidth * 4));

    if (writeSucceeded != 0)
    {
        CH_CORE_INFO(
            "Frame capture saved to: {}",
            outputPath);
    }
    else
    {
        CH_CORE_ERROR(
            "Frame capture failed to write PNG: {}",
            outputPath);
    }

    frameResource.captureBuffer.reset();
    frameResource.capturePath.clear();
    frameResource.captureWidth = 0;
    frameResource.captureHeight = 0;
    frameResource.captureFormat =
        VK_FORMAT_UNDEFINED;
    frameResource.capturePending = false;
}

void Renderer::WaitForAllFrames()
{
    VkDevice device = VulkanContext::Get().GetDevice();
    if (device == VK_NULL_HANDLE)
    {
        return;
    }

    for (auto& resource : m_FrameResources)
    {
        if (resource.inFlightFence != VK_NULL_HANDLE)
        {
            vkWaitForFences(device, 1, &resource.inFlightFence, VK_TRUE,
                            UINT64_MAX);
        }
    }
    vkDeviceWaitIdle(device);
}

VkCommandBuffer Renderer::BeginFrame()
{
    if (m_NeedResize)
    {
        // [STABILITY] Must wait for everything to finish before recreating
        // swapchain
        WaitForAllFrames();
        RecreateSwapchain();
        return VK_NULL_HANDLE;
    }

    VkDevice device = VulkanContext::Get().GetDevice();
    auto& frameResource = m_FrameResources[m_CurrentFrameIndex];

    // Wait for previous work on this frame resource to finish
    VkResult waitResult = vkWaitForFences(
        device, 1, &frameResource.inFlightFence, VK_TRUE, UINT64_MAX);
    if (waitResult != VK_SUCCESS)
    {
        CH_CORE_ERROR("Renderer: vkWaitForFences failed with error {0}",
                      (int)waitResult);
        return VK_NULL_HANDLE;
    }

    ProcessCompletedFrameCapture(frameResource);

    VulkanContext::Get().GetDeletionQueue().FlushFrame(m_CurrentFrameIndex);
    ResourceManager::Get().UpdateFrameIndex(m_CurrentFrameIndex);
    ResourceManager::Get().ClearResourceFreeQueue(m_CurrentFrameIndex);

    VkResult result =
        vkAcquireNextImageKHR(device, VulkanContext::Get().GetSwapChain(),
                              UINT64_MAX, frameResource.imageAvailableSemaphore,
                              VK_NULL_HANDLE, &m_CurrentImageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        WaitForAllFrames();
        RecreateSwapchain();
        return VK_NULL_HANDLE;
    }
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        CH_CORE_ERROR("Renderer: vkAcquireNextImageKHR failed!");
        return VK_NULL_HANDLE;
    }

    VK_CHECK(vkResetFences(device, 1, &frameResource.inFlightFence));

    // [MODERN] Reset transient pool for the new frame construction
    ResourceManager::Get().ResetTransientDescriptorPool();

    VK_CHECK(vkResetCommandBuffer(frameResource.commandBuffer, 0));
    VkCommandBufferBeginInfo beginInfo{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
    VK_CHECK(vkBeginCommandBuffer(frameResource.commandBuffer, &beginInfo));
    VkImage image =
        VulkanContext::Get().GetSwapChainImages()[m_CurrentImageIndex];

    // 1. Transition to TRANSFER_DST for clearing
    VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    vkCmdPipelineBarrier(
        frameResource.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    // 2. [DEBUG] Manually clear the swapchain to YELLOW
    VkClearColorValue yellow = {{1.0f, 1.0f, 0.0f, 1.0f}};
    VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(frameResource.commandBuffer, image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &yellow, 1,
                         &range);

    // 3. Transition to COLOR_ATTACHMENT_OPTIMAL for RenderGraph/ImGui
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

    vkCmdPipelineBarrier(frameResource.commandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &barrier);

    m_IsFrameInProgress = true;
    m_ActiveCommandBuffer = frameResource.commandBuffer;
    return frameResource.commandBuffer;
}

void Renderer::EndFrame()
{
    auto& frameResource = m_FrameResources[m_CurrentFrameIndex];

    // [FIX] Final Layout Transition: COLOR_ATTACHMENT -> PRESENT_SRC
    VkImage image =
        VulkanContext::Get().GetSwapChainImages()[m_CurrentImageIndex];
    VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = 0;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    vkCmdPipelineBarrier(frameResource.commandBuffer,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);

    VK_CHECK(vkEndCommandBuffer(frameResource.commandBuffer));
    m_ActiveCommandBuffer = VK_NULL_HANDLE;
    VkSemaphore waitSemaphores[] = {frameResource.imageAvailableSemaphore};
    VkPipelineStageFlags waitStages[] = {
        VK_PIPELINE_STAGE_TRANSFER_BIT};
    VkSemaphore signalSemaphores[] = {frameResource.renderFinishedSemaphore};
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO,
                            nullptr,
                            1,
                            waitSemaphores,
                            waitStages,
                            1,
                            &frameResource.commandBuffer,
                            1,
                            signalSemaphores};

    {
        // [FIX] Use static global mutex to ensure sync across ALL
        // contexts/instances
        std::lock_guard<std::mutex> lock(VulkanContext::GetGlobalQueueMutex());
        VK_CHECK(vkQueueSubmit(VulkanContext::Get().GetGraphicsQueue(), 1,
                               &submitInfo, frameResource.inFlightFence));

        VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                     nullptr,
                                     1,
                                     signalSemaphores,
                                     1,
                                     nullptr,
                                     nullptr,
                                     nullptr};
        VkSwapchainKHR swapChains[] = {VulkanContext::Get().GetSwapChain()};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &m_CurrentImageIndex;

        VkResult result = vkQueuePresentKHR(
            VulkanContext::Get().GetPresentQueue(), &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
            m_NeedResize)
        {
            m_NeedResize = true;
        }
    }

    m_CurrentFrameIndex = (m_CurrentFrameIndex + 1) % MaxFramesInFlight;
    m_IsFrameInProgress = false;
}

void Renderer::RecreateSwapchain()
{
    VulkanContext::Get().RecreateSwapChain();
    m_NeedResize = false;
}
} // namespace Chimera
