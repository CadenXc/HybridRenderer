#include "pch.h"
#include "Renderer.h"

namespace Chimera {

    // Helper constant, strictly internal to Renderer unless we want to expose it later
    const int MAX_FRAMES_IN_FLIGHT = 3;

    Renderer::Renderer(const std::shared_ptr<VulkanContext>& context)
        : m_Context(context)
    {
        RecreateSwapChain(); // Ensure swapchain and related resources are ready
        CreateCommandBuffers();

        // Create Sync Objects
        m_ImageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        m_RenderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        m_InFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
        m_ImagesInFlight.resize(m_Context->GetSwapChainImages().size(), VK_NULL_HANDLE);

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            if (vkCreateSemaphore(m_Context->GetDevice(), &semaphoreInfo, nullptr, &m_ImageAvailableSemaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(m_Context->GetDevice(), &semaphoreInfo, nullptr, &m_RenderFinishedSemaphores[i]) != VK_SUCCESS ||
                vkCreateFence(m_Context->GetDevice(), &fenceInfo, nullptr, &m_InFlightFences[i]) != VK_SUCCESS)
            {
                throw std::runtime_error("failed to create synchronization objects for a frame!");
            }
        }
    }

    Renderer::~Renderer()
    {
        FreeCommandBuffers();

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            vkDestroySemaphore(m_Context->GetDevice(), m_RenderFinishedSemaphores[i], nullptr);
            vkDestroySemaphore(m_Context->GetDevice(), m_ImageAvailableSemaphores[i], nullptr);
            vkDestroyFence(m_Context->GetDevice(), m_InFlightFences[i], nullptr);
        }
    }

    void Renderer::RecreateSwapChain()
    {
        // Handle minimization
        int width = 0, height = 0;
        glfwGetFramebufferSize(m_Context->GetWindow(), &width, &height);
        while (width == 0 || height == 0)
        {
            glfwGetFramebufferSize(m_Context->GetWindow(), &width, &height);
            glfwWaitEvents();
        }

        vkDeviceWaitIdle(m_Context->GetDevice());
        
        m_Context->RecreateSwapChain();
        
        // If the swapchain image count changed, we might need to resize m_ImagesInFlight
        // But for now, VulkanContext manages the swapchain logic mostly. 
        // We just need to ensure our image references are valid.
        // Actually, m_ImagesInFlight depends on swapChainImages size.
        m_ImagesInFlight.resize(m_Context->GetSwapChainImages().size(), VK_NULL_HANDLE);
    }

    void Renderer::CreateCommandBuffers()
    {
        m_CommandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = m_Context->GetCommandPool();
        allocInfo.commandBufferCount = (uint32_t)m_CommandBuffers.size();

        if (vkAllocateCommandBuffers(m_Context->GetDevice(), &allocInfo, m_CommandBuffers.data()) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to allocate command buffers!");
        }
    }

    void Renderer::FreeCommandBuffers()
    {
        vkFreeCommandBuffers(
            m_Context->GetDevice(), 
            m_Context->GetCommandPool(), 
            static_cast<uint32_t>(m_CommandBuffers.size()), 
            m_CommandBuffers.data());
        m_CommandBuffers.clear();
    }

    VkCommandBuffer Renderer::BeginFrame()
    {
        assert(!m_IsFrameStarted && "Can't call BeginFrame while already in progress");

        VkResult result = vkWaitForFences(m_Context->GetDevice(), 1, &m_InFlightFences[m_CurrentFrameIndex], VK_TRUE, UINT64_MAX);
        if(result != VK_SUCCESS) throw std::runtime_error("BeginFrame: Failed to wait for fences");

        result = vkAcquireNextImageKHR(
            m_Context->GetDevice(),
            m_Context->GetSwapChain(),
            UINT64_MAX,
            m_ImageAvailableSemaphores[m_CurrentFrameIndex],
            VK_NULL_HANDLE,
            &m_CurrentImageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            RecreateSwapChain();
            // We can't render this frame because the swapchain is invalid. 
            // Return null to indicate we should skip this frame.
            return nullptr; 
        }
        else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        {
            throw std::runtime_error("failed to acquire swap chain image!");
        }

        // Check if a previous frame is using this image (i.e. there is its fence to wait on)
        if (m_ImagesInFlight[m_CurrentImageIndex] != VK_NULL_HANDLE)
        {
            vkWaitForFences(m_Context->GetDevice(), 1, &m_ImagesInFlight[m_CurrentImageIndex], VK_TRUE, UINT64_MAX);
        }
        // Mark the image as now being in use by this frame
        m_ImagesInFlight[m_CurrentImageIndex] = m_InFlightFences[m_CurrentFrameIndex];

        VkCommandBuffer commandBuffer = m_CommandBuffers[m_CurrentFrameIndex];
        
        vkResetFences(m_Context->GetDevice(), 1, &m_InFlightFences[m_CurrentFrameIndex]);
        vkResetCommandBuffer(commandBuffer, 0);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        
        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to begin recording command buffer!");
        }

        m_IsFrameStarted = true;
        return commandBuffer;
    }

    void Renderer::EndFrame()
    {
        assert(m_IsFrameStarted && "Can't call EndFrame while frame is not in progress");
        
        VkCommandBuffer commandBuffer = m_CommandBuffers[m_CurrentFrameIndex];
        
        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to record command buffer!");
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore waitSemaphores[] = { m_ImageAvailableSemaphores[m_CurrentFrameIndex] };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        VkSemaphore signalSemaphores[] = { m_RenderFinishedSemaphores[m_CurrentFrameIndex] };
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        if (vkQueueSubmit(m_Context->GetGraphicsQueue(), 1, &submitInfo, m_InFlightFences[m_CurrentFrameIndex]) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to submit draw command buffer!");
        }

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;

        VkSwapchainKHR swapChains[] = { m_Context->GetSwapChain() };
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &m_CurrentImageIndex;

        VkResult result = vkQueuePresentKHR(m_Context->GetPresentQueue(), &presentInfo);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) // removed framebufferResized check for now, can add back via callback if needed
        {
            RecreateSwapChain();
        }
        else if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to present swap chain image!");
        }

        m_CurrentFrameIndex = (m_CurrentFrameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
        m_IsFrameStarted = false;
    }

}
