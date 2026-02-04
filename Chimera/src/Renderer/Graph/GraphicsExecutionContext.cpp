#include "pch.h"
#include "GraphicsExecutionContext.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Resources/ResourceManager.h"

namespace Chimera {

	void GraphicsExecutionContext::BindVertexBuffer(VkBuffer buffer, VkDeviceSize offset) {
		vkCmdBindVertexBuffers(m_CommandBuffer, 0, 1, &buffer, &offset);
	}

	void GraphicsExecutionContext::BindIndexBuffer(VkBuffer buffer, VkDeviceSize offset, VkIndexType type) {
		vkCmdBindIndexBuffer(m_CommandBuffer, buffer, offset, type);
	}

	void GraphicsExecutionContext::BindGlobalSet(uint32_t slot, uint32_t frameIndex) {
		VkDescriptorSet set = m_ResourceManager.GetGlobalDescriptorSet(frameIndex);
		vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.layout, slot, 1, &set, 0, nullptr);
	}

	void GraphicsExecutionContext::BindPassSet(uint32_t slot, VkDescriptorSet set) {
		if (set != VK_NULL_HANDLE) {
			vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.layout, slot, 1, &set, 0, nullptr);
		}
	}

	glm::uvec2 GraphicsExecutionContext::GetDisplaySize() {
		auto extent = m_Context.GetSwapChainExtent();
		return { extent.width, extent.height };
	}

	void GraphicsExecutionContext::SetScissor(VkRect2D scissor) {
		vkCmdSetScissor(m_CommandBuffer, 0, 1, &scissor);
	}

	void GraphicsExecutionContext::SetViewport(VkViewport viewport) {
		vkCmdSetViewport(m_CommandBuffer, 0, 1, &viewport);
	}

	void GraphicsExecutionContext::SetDepthBias(float depthBiasConstantFactor, float depthBiasClamp,
		float depthBiasSlopeFactor) {
		vkCmdSetDepthBias(m_CommandBuffer, depthBiasConstantFactor, depthBiasClamp, depthBiasSlopeFactor);
	}

	void GraphicsExecutionContext::DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
		uint32_t firstIndex, uint32_t vertexOffset, uint32_t firstInstance) {
		vkCmdDrawIndexed(m_CommandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
	}

	void GraphicsExecutionContext::Draw(uint32_t vertexCount, uint32_t instanceCount,
		uint32_t firstVertex, uint32_t firstInstance) {
		vkCmdDraw(m_CommandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
	}

}
