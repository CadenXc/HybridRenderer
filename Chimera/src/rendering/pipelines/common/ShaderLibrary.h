#pragma once

#include "pch.h"

namespace Chimera {

	class ShaderLibrary
	{
	public:
		/**
		 * @brief Load and create a VkShaderModule.
		 * Automatically handles path appending (.spv) and file reading.
		 */
		static VkShaderModule LoadShader(VkDevice device, const std::string& name);

		/**
		 * @brief Helper function: Create VkPipelineShaderStageCreateInfo based on filename and stage.
		 */
		static VkPipelineShaderStageCreateInfo CreateShaderStage(VkDevice device, 
																const std::string& name, 
																VkShaderStageFlagBits stage);
	};

}