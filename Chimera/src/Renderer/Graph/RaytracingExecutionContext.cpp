#include "pch.h"
#include "RaytracingExecutionContext.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Resources/ResourceManager.h"

namespace Chimera {

	void RaytracingExecutionContext::TraceRays(uint32_t width, uint32_t height) {
		vkCmdTraceRaysKHR(m_CommandBuffer,
			&m_Pipeline.raygen_sbt.strided_device_address_region,
			&m_Pipeline.miss_sbt.strided_device_address_region,
			&m_Pipeline.hit_sbt.strided_device_address_region,
			&m_Pipeline.call_sbt.strided_device_address_region,
			width, height, 1
		);
	}

	void RaytracingExecutionContext::BindGlobalSet(uint32_t slot, uint32_t frameIndex) {
		VkDescriptorSet set = m_ResourceManager.GetGlobalDescriptorSet(frameIndex);
		vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_Pipeline.layout, slot, 1, &set, 0, nullptr);
	}

	void RaytracingExecutionContext::BindPassSet(uint32_t slot, VkDescriptorSet set) {
		if (set != VK_NULL_HANDLE) {
			vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_Pipeline.layout, slot, 1, &set, 0, nullptr);
		}
	}

	glm::uvec2 RaytracingExecutionContext::GetDisplaySize() {
		auto extent = m_Context.GetSwapChainExtent();
		return { extent.width, extent.height };
	}

}
