#include "pch.h"
#include "ComputeExecutionContext.h"
#include "gfx/resources/ResourceManager.h"
#include "gfx/vulkan/VulkanContext.h"
#include "gfx/utils/VulkanShaderUtils.h" // Assuming VulkanUtils are here or similar

namespace Chimera {

	glm::uvec2 ComputeExecutionContext::GetDisplaySize() {
		return { m_RenderGraph.m_Context.GetSwapChainExtent().width, m_RenderGraph.m_Context.GetSwapChainExtent().height };
	}

	void ComputeExecutionContext::Dispatch(const char* shader, uint32_t xGroups, uint32_t yGroups, uint32_t zGroups) {
		ComputePipeline& pipeline = m_RenderGraph.m_ComputePipelines[shader];

		vkCmdBindPipeline(m_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.handle);
		vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
			pipeline.layout, 0, 1, &m_ResourceManager.GetGlobalDescriptorSet0(), 0, nullptr);
		vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
			pipeline.layout, 1, 1, &m_ResourceManager.GetGlobalDescriptorSet1(), 0, nullptr);
		
		// Note: GetPerFrameDescriptorSets returns const vector ref
		const auto& perFrameSets = m_ResourceManager.GetPerFrameDescriptorSets();
		vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
			pipeline.layout, 2, 1, &perFrameSets[m_ResourceIdx], 0, nullptr);

		if (m_RenderPass.descriptor_set != VK_NULL_HANDLE) {
			vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout,
				3, 1, &m_RenderPass.descriptor_set, 0, nullptr);
		}

		vkCmdDispatch(m_CommandBuffer, xGroups, yGroups, zGroups);
	}

	/*
	void ComputeExecutionContext::BlitImageStorageToTransient(int src, const char *dst) {
		// Implementation commented out
	}
	// ... other blit implementations ...
	*/

}
