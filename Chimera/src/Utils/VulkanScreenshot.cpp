#include "pch.h"
#include "VulkanScreenshot.h"
#include "Utils/VulkanBarrier.h"
#include "Renderer/Backend/RenderContext.h"
#include "Renderer/Resources/Buffer.h"
#include <fstream>

namespace Chimera {

	void VulkanScreenshot::SaveToPPM(std::shared_ptr<VulkanContext> context, 
									 VkImage sourceImage, 
									 VkFormat sourceImageFormat, 
									 VkExtent2D extent, 
									 VkImageLayout currentLayout, 
									 const std::string& filename)
	{
		VkDevice device = context->GetDevice();
		
		// 1. Create Staging Buffer
		VkDeviceSize imageSize = extent.width * extent.height * 4;
		Buffer stagingBuffer(context->GetAllocator(), imageSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);

		// 2. Execute Copy Command
		{
			ScopedCommandBuffer cmd(context);

			// Transition to TRANSFER_SRC_OPTIMAL
			VkImageMemoryBarrier barrier = VulkanUtils::CreateImageBarrier(
				sourceImage, 
				currentLayout, 
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 
				VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, 
				VK_ACCESS_TRANSFER_READ_BIT
			);
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

			// Copy
			VkBufferImageCopy region{};
			region.bufferOffset = 0;
			region.bufferRowLength = 0;
			region.bufferImageHeight = 0;
			region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource.mipLevel = 0;
			region.imageSubresource.baseArrayLayer = 0;
			region.imageSubresource.layerCount = 1;
			region.imageOffset = { 0, 0, 0 };
			region.imageExtent = { extent.width, extent.height, 1 };

			vkCmdCopyImageToBuffer(cmd, sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer.GetBuffer(), 1, &region);

			// Transition back to original layout (or expected next layout)
			VkImageMemoryBarrier barrier2 = VulkanUtils::CreateImageBarrier(
				sourceImage, 
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 
				currentLayout, 
				VK_ACCESS_TRANSFER_READ_BIT, 
				VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT
			);
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier2);
		}

		// 3. Read Memory
		const char* data = (const char*)stagingBuffer.Map();
		
		// 4. Write PPM
		std::ofstream file(filename, std::ios::out | std::ios::binary);
		if (file.is_open())
		{
			file << "P6\n" << extent.width << " " << extent.height << "\n255\n";
			
			// Convert BGRA (common swapchain) to RGB
			// Assuming B8G8R8A8_SRGB or similar. If R8G8B8A8, we need different swizzle.
			// Let's assume BGR for now as it's common on Windows. 
			// Better: Check format.
			bool isBGR = (sourceImageFormat == VK_FORMAT_B8G8R8A8_SRGB || sourceImageFormat == VK_FORMAT_B8G8R8A8_UNORM);

			std::vector<unsigned char> pixelData(extent.width * extent.height * 3);
			for (uint32_t i = 0; i < extent.width * extent.height; i++)
			{
				if (isBGR) {
					pixelData[i * 3 + 0] = data[i * 4 + 2]; // R
					pixelData[i * 3 + 1] = data[i * 4 + 1]; // G
					pixelData[i * 3 + 2] = data[i * 4 + 0]; // B
				} else {
					pixelData[i * 3 + 0] = data[i * 4 + 0]; // R
					pixelData[i * 3 + 1] = data[i * 4 + 1]; // G
					pixelData[i * 3 + 2] = data[i * 4 + 2]; // B
				}
			}
			
			file.write((const char*)pixelData.data(), pixelData.size());
			file.close();
			CH_CORE_INFO("Saved screenshot to {}", filename);
		}
		else
		{
			CH_CORE_ERROR("Failed to open file for screenshot: {}", filename);
		}

		stagingBuffer.Unmap();
	}

}
