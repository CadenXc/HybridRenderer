#include "pch.h"
#include "RaytracingExecutionContext.h"

namespace Chimera {

	void RaytracingExecutionContext::TraceRays(uint32_t width, uint32_t height) {
		VkStridedDeviceAddressRegionKHR callable_sbt{};

		vkCmdTraceRaysKHR(m_CommandBuffer,
			&m_Pipeline.raygen_sbt.strided_device_address_region,
			&m_Pipeline.miss_sbt.strided_device_address_region,
			&m_Pipeline.hit_sbt.strided_device_address_region,
			&callable_sbt, width, height, 1
		);
	}

}
