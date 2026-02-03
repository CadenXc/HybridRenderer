#pragma once

#include "pch.h"
#include "gfx/vulkan/VulkanCommon.h"

namespace Chimera {

	class ResourceManager;
	class GraphicsExecutionContext {
	public:
		GraphicsExecutionContext(VkCommandBuffer commandBuffer, ResourceManager& resourceManager,
			GraphicsPipeline& pipeline) :
			m_CommandBuffer(commandBuffer),
			m_ResourceManager(resourceManager),
			m_Pipeline(pipeline) {}

		void BindVertexBuffer(VkBuffer buffer, VkDeviceSize offset);
		void BindIndexBuffer(VkBuffer buffer, VkDeviceSize offset, VkIndexType type);
		void SetScissor(VkRect2D scissor);
		void SetViewport(VkViewport viewport);
		void SetDepthBias(float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor);
		void DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex,
			uint32_t vertexOffset, uint32_t firstInstance);
		void Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex,
			uint32_t firstInstance);

		template<typename T>
		void PushConstants(T& pushConstants) {
			assert(sizeof(T) == m_Pipeline.description.push_constants.size);
			vkCmdPushConstants(m_CommandBuffer, m_Pipeline.layout, m_Pipeline.description.push_constants.shader_stage,
				0, m_Pipeline.description.push_constants.size, &pushConstants);
		}

		VkCommandBuffer GetCommandBuffer() const { return m_CommandBuffer; }
		VkPipelineLayout GetPipelineLayout() const { return m_Pipeline.layout; }

	private:
		VkCommandBuffer m_CommandBuffer;
		ResourceManager& m_ResourceManager;
		GraphicsPipeline& m_Pipeline;
	};

}