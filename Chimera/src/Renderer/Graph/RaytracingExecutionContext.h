#pragma once

#include "pch.h"
#include "Renderer/Backend/VulkanCommon.h"

namespace Chimera {

	class VulkanContext;
	class ResourceManager;
	class RaytracingExecutionContext {
	public:
		RaytracingExecutionContext(VkCommandBuffer commandBuffer, VulkanContext& context, ResourceManager& resourceManager,
			RaytracingPipeline& pipeline) :
			m_CommandBuffer(commandBuffer),
			m_Context(context),
			m_ResourceManager(resourceManager),
			m_Pipeline(pipeline)
		{}

		void TraceRays(uint32_t width, uint32_t height);

		void BindGlobalSet(uint32_t slot, uint32_t frameIndex);
		void BindPassSet(uint32_t slot, VkDescriptorSet set);

		glm::uvec2 GetDisplaySize();

		VkCommandBuffer GetCommandBuffer() const { return m_CommandBuffer; }
		VkPipelineLayout GetPipelineLayout() const { return m_Pipeline.layout; }

	private:
		VkCommandBuffer m_CommandBuffer;
		VulkanContext& m_Context;
		ResourceManager& m_ResourceManager;
		RaytracingPipeline& m_Pipeline;
	};

}
