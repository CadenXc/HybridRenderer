#include "pch.h"
#include "Utils/VulkanShaderUtils.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Backend/RenderContext.h"
#include "Core/FileIO.h"

namespace Chimera::VulkanUtils {

	VkShaderModule LoadShaderModule(const std::string& filename, VkDevice device) 
	{
		auto code = FileIO::ReadFile(filename);
		
		VkShaderModuleCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = code.size();
		createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

		VkShaderModule shaderModule;
		if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to create shader module: " + filename);
		}
		return shaderModule;
	}

	void CopyBuffer(
		std::shared_ptr<VulkanContext> context,
		VkBuffer srcBuffer,
		VkBuffer dstBuffer,
		VkDeviceSize size
	) 
	{
		ScopedCommandBuffer cmd(context);
		VkBufferCopy copyRegion{};
		copyRegion.size = size;
		vkCmdCopyBuffer(cmd, srcBuffer, dstBuffer, 1, &copyRegion);
	}
}

