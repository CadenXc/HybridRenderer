#pragma once

#include "pch.h"
#include "Renderer/Backend/VulkanContext.h"
#include <string>
#include <vector>

namespace Chimera {

	class VulkanScreenshot {
	public:
		static void SaveToPPM(std::shared_ptr<VulkanContext> context, 
							  VkImage sourceImage, 
							  VkFormat sourceImageFormat, 
							  VkExtent2D extent, 
							  VkImageLayout currentLayout, 
							  const std::string& filename);
	};

}
