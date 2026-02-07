#pragma once
#include "pch.h"
#include "Swapchain.h"
#include "VulkanInstance.h"
#include "VulkanDevice.h"

namespace Chimera {

	class VulkanContext
	{
	public:
		VulkanContext(GLFWwindow* window);
		~VulkanContext();

		VulkanContext(const VulkanContext&) = delete;
		VulkanContext& operator=(const VulkanContext&) = delete;

		GLFWwindow* GetWindow() const { return m_Window; }
		VkDevice GetDevice() const { return m_Device->GetHandle(); }
		VkPhysicalDevice GetPhysicalDevice() const { return m_Device->GetPhysicalDevice(); }
		VkInstance GetInstance() const { return m_Instance->GetHandle(); }
		VkQueue GetGraphicsQueue() const { return m_Device->GetGraphicsQueue(); }
		uint32_t GetGraphicsQueueFamily() const { return m_Device->GetGraphicsQueueFamily(); }
		VkQueue GetPresentQueue() const { return m_Device->GetPresentQueue(); }
		VkSurfaceKHR GetSurface() const { return m_Surface; }
		VmaAllocator GetAllocator() const { return m_Device->GetAllocator(); }
		VkCommandPool GetCommandPool() const { return m_CommandPool; }
		VkSampleCountFlagBits GetMSAASamples() const { return m_Device->GetMaxUsableSampleCount(); }
		const VkPhysicalDeviceProperties& GetDeviceProperties() const { return m_Device->GetProperties(); }
		const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& GetRayTracingProperties() const { return m_Device->GetRTProperties(); }
		bool IsRayTracingSupported() const { return m_Device->IsRayTracingSupported(); }

		// Swapchain Access
		std::shared_ptr<Swapchain> GetSwapchain() const { return m_Swapchain; }
		void RecreateSwapChain() { m_Swapchain->Recreate(); }
		
		VkSwapchainKHR GetSwapChain() const { return m_Swapchain->GetHandle(); }
		uint32_t GetSwapChainImageCount() const { return m_Swapchain->GetImageCount(); }
		VkFormat GetSwapChainImageFormat() const { return m_Swapchain->GetFormat(); }
		VkExtent2D GetSwapChainExtent() const { return m_Swapchain->GetExtent(); }
		const std::vector<VkImage>& GetSwapChainImages() const { return m_Swapchain->GetImages(); }
		const std::vector<VkImageView>& GetSwapChainImageViews() const { return m_Swapchain->GetImageViews(); }

		VkImageView CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels);

		uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) { return m_Device->FindMemoryType(typeFilter, properties); }
		VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) { return m_Device->FindSupportedFormat(candidates, tiling, features); }

	private:
		void CreateSurface();
		void CreateCommandPool();

	private:
		GLFWwindow* m_Window;
		std::unique_ptr<VulkanInstance> m_Instance;
		VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
		std::unique_ptr<VulkanDevice> m_Device;
		std::shared_ptr<Swapchain> m_Swapchain;
		VkCommandPool m_CommandPool = VK_NULL_HANDLE;
	};
}