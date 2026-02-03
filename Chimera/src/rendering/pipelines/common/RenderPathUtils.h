#pragma once

#include "pch.h"
#include "gfx/vulkan/VulkanContext.h"

namespace Chimera {

	class RenderPathUtils
	{
	public:
		static void ExecuteSingleTimeCommands(std::shared_ptr<VulkanContext> context, 
											std::function<void(VkCommandBuffer)> recordCallback);

		static void TransitionImageLayout(std::shared_ptr<VulkanContext> context, 
										VkImage image, 
										VkFormat format, 
										VkImageLayout oldLayout, 
										VkImageLayout newLayout, 
										uint32_t mipLevels = 1);

		static void TransitionImageLayout(VkCommandBuffer cmd,
										VkImage image,
										VkFormat format,
										VkImageLayout oldLayout,
										VkImageLayout newLayout,
										uint32_t mipLevels = 1);

		static void BlitToSwapchain(VkCommandBuffer cmd, 
								   std::shared_ptr<VulkanContext> context,
								   VkImage srcImage, 
								   VkImage dstImage, 
								   VkExtent2D extent);
	};

}