#pragma once

#include "pch.h"
#include "gfx/vulkan/VulkanCommon.h"

namespace Chimera {

	class ResourceManager;
	class RaytracingExecutionContext {
	public:
		RaytracingExecutionContext(VkCommandBuffer commandBuffer, ResourceManager& resourceManager,
			RaytracingPipeline& pipeline) :
			m_CommandBuffer(commandBuffer),
			m_ResourceManager(resourceManager),
			m_Pipeline(pipeline)
		{}

		void TraceRays(uint32_t width, uint32_t height);

	private:
		VkCommandBuffer m_CommandBuffer;
		ResourceManager& m_ResourceManager;
		RaytracingPipeline& m_Pipeline;
	};

}
