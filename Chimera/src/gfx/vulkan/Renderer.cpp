#include "pch.h"
#include "gfx/vulkan/Renderer.h"

// Helper macro for Vulkan error checking
#define VK_CHECK(result) \
	do { \
		VkResult res = (result); \
		if (res != VK_SUCCESS) { \
			throw std::runtime_error("Vulkan error: " + std::to_string(res)); \
		} \
	} while(0)

namespace Chimera {

	// Frame resource structure for synchronization
	struct FrameResource
	{
		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
		VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
		VkFence inFlightFence = VK_NULL_HANDLE;
	};

	Renderer::Renderer(const std::shared_ptr<VulkanContext>& context)
		: m_Context(context)
	{
		// Create frame resources for triple buffering
		CreateFrameResources();
	}

	Renderer::~Renderer()
	{
		// Ensure GPU completes all work before destroying
		if (m_Context)
		{
			vkDeviceWaitIdle(m_Context->GetDevice());
		}
		FreeFrameResources();
	}

	void Renderer::CreateFrameResources()
	{
		VkDevice device = m_Context->GetDevice();
		VkCommandPool commandPool = m_Context->GetCommandPool();
		
		m_FrameResources.resize(MaxFramesInFlight);

		for (int i = 0; i < MaxFramesInFlight; ++i)
		{
			// Create command buffer
			VkCommandBufferAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocInfo.commandPool = commandPool;
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocInfo.commandBufferCount = 1;

			if (vkAllocateCommandBuffers(device, &allocInfo, &m_FrameResources[i].commandBuffer) != VK_SUCCESS)
			{
				throw std::runtime_error("failed to allocate command buffers!");
			}

			// Create semaphores and fence
			VkSemaphoreCreateInfo semaphoreInfo{};
			semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

			if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_FrameResources[i].imageAvailableSemaphore) != VK_SUCCESS ||
				vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_FrameResources[i].renderFinishedSemaphore) != VK_SUCCESS)
			{
				throw std::runtime_error("failed to create semaphores!");
			}

			VkFenceCreateInfo fenceInfo{};
			fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

			if (vkCreateFence(device, &fenceInfo, nullptr, &m_FrameResources[i].inFlightFence) != VK_SUCCESS)
			{
				throw std::runtime_error("failed to create fence!");
			}
		}
	}

	void Renderer::FreeFrameResources()
	{
		VkDevice device = m_Context->GetDevice();
		VkCommandPool commandPool = m_Context->GetCommandPool();

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
			if (frameResource.commandBuffer != VK_NULL_HANDLE)
			{
				vkFreeCommandBuffers(device, commandPool, 1, &frameResource.commandBuffer);
			}
		}
		m_FrameResources.clear();
	}

	void Renderer::OnResize(uint32_t width, uint32_t height)
	{
		m_NeedResize = true;
	}

	VkCommandBuffer Renderer::BeginFrame()
	{
		assert(!m_IsFrameInProgress && "Cannot call BeginFrame while frame is already in progress!");

		// 1. Handle window resize
		if (m_NeedResize)
		{
			RecreateSwapchain();
			return VK_NULL_HANDLE; // Skip this frame
		}

		VkDevice device = m_Context->GetDevice();
		auto& frameResource = m_FrameResources[m_CurrentFrameIndex];

		// 2. Wait for previous frame GPU work to complete
		VK_CHECK(vkWaitForFences(device, 1, &frameResource.inFlightFence, VK_TRUE, UINT64_MAX));

		// 3. Get next swapchain image
		VkResult result = vkAcquireNextImageKHR(
			device,
			m_Context->GetSwapChain(),
			UINT64_MAX,
			frameResource.imageAvailableSemaphore,
			VK_NULL_HANDLE,
			&m_CurrentImageIndex
		);

		// Handle swapchain being out of date
		if (result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			RecreateSwapchain();
			return VK_NULL_HANDLE;
		}
		else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
		{
			throw std::runtime_error("failed to acquire swap chain image!");
		}

		// 4. Reset fence after successful acquire
		VK_CHECK(vkResetFences(device, 1, &frameResource.inFlightFence));

		// 5. Reset and begin command buffer
		VK_CHECK(vkResetCommandBuffer(frameResource.commandBuffer, 0));

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		VK_CHECK(vkBeginCommandBuffer(frameResource.commandBuffer, &beginInfo));

		m_IsFrameInProgress = true;
		m_ActiveCommandBuffer = frameResource.commandBuffer;
		return frameResource.commandBuffer;
	}

	void Renderer::EndFrame()
	{
		assert(m_IsFrameInProgress && "Cannot call EndFrame while frame is not in progress!");

		auto& frameResource = m_FrameResources[m_CurrentFrameIndex];
		VkCommandBuffer commandBuffer = frameResource.commandBuffer;

		// 1. End command buffer recording
		VK_CHECK(vkEndCommandBuffer(commandBuffer));
		m_ActiveCommandBuffer = VK_NULL_HANDLE;

		// 2. Submit commands
		VkSemaphore waitSemaphores[] = { frameResource.imageAvailableSemaphore };
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		VkSemaphore signalSemaphores[] = { frameResource.renderFinishedSemaphore };

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphores;

		// Submit with fence for CPU synchronization
		VK_CHECK(vkQueueSubmit(m_Context->GetGraphicsQueue(), 1, &submitInfo, frameResource.inFlightFence));

		// 3. Present
		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = signalSemaphores;

		VkSwapchainKHR swapChains[] = { m_Context->GetSwapChain() };
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = swapChains;
		presentInfo.pImageIndices = &m_CurrentImageIndex;

		VkResult result = vkQueuePresentKHR(m_Context->GetPresentQueue(), &presentInfo);

		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_NeedResize)
		{
			m_NeedResize = true; // Handle on next frame
		}
		else if (result != VK_SUCCESS)
		{
			throw std::runtime_error("failed to present swap chain image!");
		}

		// 4. Update frame index for next frame
		m_CurrentFrameIndex = (m_CurrentFrameIndex + 1) % MaxFramesInFlight;
		m_IsFrameInProgress = false;
	}

	void Renderer::RecreateSwapchain()
	{
		GLFWwindow* window = m_Context->GetWindow();
		int width = 0, height = 0;
		glfwGetFramebufferSize(window, &width, &height);
		while (width == 0 || height == 0)
		{
			glfwGetFramebufferSize(window, &width, &height);
			glfwWaitEvents();
		}

		vkDeviceWaitIdle(m_Context->GetDevice());

		// Recreate swapchain
		m_Context->RecreateSwapChain();

		m_NeedResize = false;
	}

}
