#pragma once

#include "pch.h"
#include "Renderer/Backend/VulkanCommon.h"

namespace Chimera {

	class VulkanContext;
	class ResourceManager;
	class GraphicsExecutionContext {
	public:
		GraphicsExecutionContext(VkCommandBuffer commandBuffer, VulkanContext& context, ResourceManager& resourceManager,
			GraphicsPipeline& pipeline) :
			m_CommandBuffer(commandBuffer),
			m_Context(context),
			m_ResourceManager(resourceManager),
			m_Pipeline(pipeline) {}

		void BindVertexBuffer(VkBuffer buffer, VkDeviceSize offset);
		void BindIndexBuffer(VkBuffer buffer, VkDeviceSize offset, VkIndexType type);
		
		// 新增：动态绑定方�?
		void BindGlobalSet(uint32_t slot, uint32_t frameIndex);
		void BindPassSet(uint32_t slot, VkDescriptorSet set);

		glm::uvec2 GetDisplaySize();

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
		VulkanContext& m_Context;
		ResourceManager& m_ResourceManager;
		GraphicsPipeline& m_Pipeline;
	};

}
