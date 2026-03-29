#include "pch.h"
#include "Renderer/Backend/Renderer.h"
#include "Renderer/Backend/RenderContext.h"
#include "Renderer/Resources/ResourceManager.h"

namespace Chimera
{
    Renderer* Renderer::s_Instance = nullptr;

    struct FrameResource
    {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
        VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
        VkFence inFlightFence = VK_NULL_HANDLE;
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
        
        VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = VulkanContext::Get().GetGraphicsQueueFamily();
        
        VK_CHECK(vkCreateCommandPool(device, &poolInfo, nullptr, &m_CommandPool));

        m_FrameResources.resize(MaxFramesInFlight);

        for (int i = 0; i < MaxFramesInFlight; ++i)
        {
            VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
            allocInfo.commandPool = m_CommandPool;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = 1;

            VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &m_FrameResources[i].commandBuffer));

            VkSemaphoreCreateInfo semaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
            VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_FrameResources[i].imageAvailableSemaphore));
            VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_FrameResources[i].renderFinishedSemaphore));

            VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
            fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &m_FrameResources[i].inFlightFence));
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
                vkDestroySemaphore(device, frameResource.renderFinishedSemaphore, nullptr);
            }
            if (frameResource.imageAvailableSemaphore != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(device, frameResource.imageAvailableSemaphore, nullptr);
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
                vkWaitForFences(device, 1, &resource.inFlightFence, VK_TRUE, UINT64_MAX);
            }
        }
        vkDeviceWaitIdle(device);
    }

    void Renderer::ResetSwapchainLayouts()
    {
        auto images = VulkanContext::Get().GetSwapChainImages();
        
        ScopedCommandBuffer cmd;
        for (auto img : images)
        {
            VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // Safest baseline state
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = 0;
            barrier.image = img;
            barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            
            vkCmdPipelineBarrier(cmd, 
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 
                0, 0, nullptr, 0, nullptr, 1, &barrier);
        }
    }

    VkCommandBuffer Renderer::BeginFrame()
    {
        if (m_NeedResize)
        {
            // [STABILITY] Must wait for everything to finish before recreating swapchain
            WaitForAllFrames();
            RecreateSwapchain();
            return VK_NULL_HANDLE;
        }

        VkDevice device = VulkanContext::Get().GetDevice();
        auto& frameResource = m_FrameResources[m_CurrentFrameIndex];
        
        // Wait for previous work on this frame resource to finish
        VkResult waitResult = vkWaitForFences(device, 1, &frameResource.inFlightFence, VK_TRUE, UINT64_MAX);
        if (waitResult != VK_SUCCESS)
        {
            CH_CORE_ERROR("Renderer: vkWaitForFences failed with error {0}", (int)waitResult);
            return VK_NULL_HANDLE;
        }

        VulkanContext::Get().GetDeletionQueue().FlushFrame(m_CurrentFrameIndex);
        ResourceManager::Get().UpdateFrameIndex(m_CurrentFrameIndex);
        ResourceManager::Get().ClearResourceFreeQueue(m_CurrentFrameIndex);

        VkResult result = vkAcquireNextImageKHR(device, VulkanContext::Get().GetSwapChain(), UINT64_MAX, frameResource.imageAvailableSemaphore, VK_NULL_HANDLE, &m_CurrentImageIndex);
        
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
        VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr };
        VK_CHECK(vkBeginCommandBuffer(frameResource.commandBuffer, &beginInfo));
        VkImage image = VulkanContext::Get().GetSwapChainImages()[m_CurrentImageIndex];

        // 1. Transition to TRANSFER_DST for clearing
        VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; 
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.image = image;
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        vkCmdPipelineBarrier(frameResource.commandBuffer, 
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
            VK_PIPELINE_STAGE_TRANSFER_BIT, 
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        // 2. [DEBUG] Manually clear the swapchain to YELLOW
        VkClearColorValue yellow = { {1.0f, 1.0f, 0.0f, 1.0f} };
        VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdClearColorImage(frameResource.commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &yellow, 1, &range);

        // 3. Transition to COLOR_ATTACHMENT_OPTIMAL for RenderGraph/ImGui
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

        vkCmdPipelineBarrier(frameResource.commandBuffer, 
            VK_PIPELINE_STAGE_TRANSFER_BIT, 
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        m_IsFrameInProgress = true;
        m_ActiveCommandBuffer = frameResource.commandBuffer;
        return frameResource.commandBuffer;
    }

    void Renderer::EndFrame()
    {
        auto& frameResource = m_FrameResources[m_CurrentFrameIndex];
        
        // [FIX] Final Layout Transition: COLOR_ATTACHMENT -> PRESENT_SRC
        VkImage image = VulkanContext::Get().GetSwapChainImages()[m_CurrentImageIndex];
        VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = 0;
        barrier.image = image;
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        
        vkCmdPipelineBarrier(frameResource.commandBuffer, 
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        VK_CHECK(vkEndCommandBuffer(frameResource.commandBuffer));
        m_ActiveCommandBuffer = VK_NULL_HANDLE;
        VkSemaphore waitSemaphores[] = { frameResource.imageAvailableSemaphore };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkSemaphore signalSemaphores[] = { frameResource.renderFinishedSemaphore };
        VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr, 1, waitSemaphores, waitStages, 1, &frameResource.commandBuffer, 1, signalSemaphores };
        
        {
            // [FIX] Global synchronization for queue operations
            std::lock_guard<std::mutex> lock(VulkanContext::Get().GetQueueMutex());
            VK_CHECK(vkQueueSubmit(VulkanContext::Get().GetGraphicsQueue(), 1, &submitInfo, frameResource.inFlightFence));
            
            VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, nullptr, 1, signalSemaphores, 1, nullptr, nullptr, nullptr };
            VkSwapchainKHR swapChains[] = { VulkanContext::Get().GetSwapChain() };
            presentInfo.swapchainCount = 1; 
            presentInfo.pSwapchains = swapChains; 
            presentInfo.pImageIndices = &m_CurrentImageIndex;
            
            VkResult result = vkQueuePresentKHR(VulkanContext::Get().GetPresentQueue(), &presentInfo);
            if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_NeedResize)
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
}
