#pragma once
#include "pch.h"

namespace Chimera {
	class VulkanContext;

	namespace VulkanUtils {

		VkShaderModule LoadShaderModule(const std::string& filename, VkDevice device);

		void CopyBuffer(
			std::shared_ptr<VulkanContext> context,
			VkBuffer srcBuffer,
			VkBuffer dstBuffer,
			VkDeviceSize size
		);
	}
}
