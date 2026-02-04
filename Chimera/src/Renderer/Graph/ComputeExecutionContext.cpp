#include "pch.h"
#include "ComputeExecutionContext.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Utils/VulkanShaderUtils.h" // Assuming VulkanUtils are here or similar

namespace Chimera {

	glm::uvec2 ComputeExecutionContext::GetDisplaySize() {
		return { m_RenderGraph.m_Context.GetSwapChainExtent().width, m_RenderGraph.m_Context.GetSwapChainExtent().height };
	}

	void ComputeExecutionContext::Dispatch(const char* shader, uint32_t xGroups, uint32_t yGroups, uint32_t zGroups) {
		ComputePipeline* pipeline = m_RenderGraph.m_ComputePipelines[shader];

		vkCmdBindPipeline(m_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->handle);
		
		// 我们默认在这里绑定，或者让用户手动�?
		BindGlobalSet(0, m_ResourceIdx, *pipeline);
		if (m_RenderPass.descriptor_set != VK_NULL_HANDLE) {
			BindPassSet(1, m_RenderPass.descriptor_set, *pipeline);
		}

		vkCmdDispatch(m_CommandBuffer, xGroups, yGroups, zGroups);
	}

	void ComputeExecutionContext::BindGlobalSet(uint32_t slot, uint32_t frameIndex, const ComputePipeline& pipeline) {
		VkDescriptorSet set = m_ResourceManager.GetGlobalDescriptorSet(frameIndex);
		vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout, slot, 1, &set, 0, nullptr);
	}

	void ComputeExecutionContext::BindPassSet(uint32_t slot, VkDescriptorSet set, const ComputePipeline& pipeline) {
		if (set != VK_NULL_HANDLE) {
			vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout, slot, 1, &set, 0, nullptr);
		}
	}

	/*
	void ComputeExecutionContext::BlitImageStorageToTransient(int src, const char *dst) {
		// Implementation commented out
	}
	// ... other blit implementations ...
	*/

}
