#include "pch.h"
#include "rendering/pipelines/common/RenderPathUtils.h"
#include "gfx/utils/VulkanBarrier.h"

namespace Chimera {

	void RenderPathUtils::ExecuteSingleTimeCommands(std::shared_ptr<VulkanContext> context, 
												  std::function<void(VkCommandBuffer)> recordCallback)
	{
		VkCommandBuffer commandBuffer = context->BeginSingleTimeCommands();
		recordCallback(commandBuffer);
		context->EndSingleTimeCommands(commandBuffer);
	}

	void RenderPathUtils::TransitionImageLayout(std::shared_ptr<VulkanContext> context, 
											  VkImage image, 
											  VkFormat format, 
											  VkImageLayout oldLayout, 
											  VkImageLayout newLayout, 
											  uint32_t mipLevels)
	{
		ExecuteSingleTimeCommands(context, [&](VkCommandBuffer cmd) {
			VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL || VulkanUtils::IsDepthFormat(format)) {
				aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			}

			VkImageMemoryBarrier barrier = VulkanUtils::CreateImageBarrier(
				image, oldLayout, newLayout, 0, 0, aspectMask);
			barrier.subresourceRange.levelCount = mipLevels;

			VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

			if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
				barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
				dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			} else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
				barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
				dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			}

			vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
		});
	}

	void RenderPathUtils::TransitionImageLayout(VkCommandBuffer cmd,
											  VkImage image,
											  VkFormat format,
											  VkImageLayout oldLayout,
											  VkImageLayout newLayout,
											  uint32_t mipLevels)
	{
		VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL || VulkanUtils::IsDepthFormat(format)) {
			aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		}

		VkImageMemoryBarrier barrier = VulkanUtils::CreateImageBarrier(
			image, oldLayout, newLayout, 0, 0, aspectMask);
		barrier.subresourceRange.levelCount = mipLevels;

		VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

		vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	}

	void RenderPathUtils::BlitToSwapchain(VkCommandBuffer cmd, 
										 std::shared_ptr<VulkanContext> context,
										 VkImage srcImage, 
										 VkImage dstImage, 
										 VkExtent2D extent)
	{
		VkImageBlit blit{};
		blit.srcOffsets[0] = { 0, 0, 0 };
		blit.srcOffsets[1] = { (int32_t)extent.width, (int32_t)extent.height, 1 };
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.mipLevel = 0;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount = 1;
		
		blit.dstOffsets[0] = { 0, 0, 0 };
		blit.dstOffsets[1] = { (int32_t)extent.width, (int32_t)extent.height, 1 };
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.mipLevel = 0;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount = 1;

		vkCmdBlitImage(cmd, 
			srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 
			dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
			1, &blit, VK_FILTER_LINEAR);
	}

}
