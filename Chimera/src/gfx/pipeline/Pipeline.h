#pragma once
#include "gfx/vulkan/VulkanCommon.h"

namespace Chimera {
	class VulkanContext;
	class ResourceManager;
	struct RenderPass;

	namespace VulkanUtils {
		GraphicsPipeline CreateGraphicsPipeline(std::shared_ptr<VulkanContext> context, ResourceManager &resource_manager,
			RenderPass &render_pass, GraphicsPipelineDescription description);

		// Stub for now or implement if needed
		RaytracingPipeline CreateRaytracingPipeline(std::shared_ptr<VulkanContext> context, ResourceManager &resource_manager,
			RenderPass &render_pass, RaytracingPipelineDescription description, 
			const VkPhysicalDeviceRayTracingPipelinePropertiesKHR &raytracing_properties);

		ComputePipeline CreateComputePipeline(std::shared_ptr<VulkanContext> context, ResourceManager &resource_manager,
			RenderPass &render_pass, PushConstantDescription push_constant_description, ComputeKernel kernel);
	}
}


