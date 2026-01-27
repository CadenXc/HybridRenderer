#pragma once
#include "pch.h"
#include "gfx/resources/ResourceManager.h"
#include "gfx/vulkan/VulkanCommon.h"

namespace Chimera
{
    class RaytracingExecutionContext
    {
    public:
        RaytracingExecutionContext(VkCommandBuffer commandBuffer, ResourceManager &resourceManager, RaytracingPipeline &pipeline)
            : m_CommandBuffer(commandBuffer), m_ResourceManager(resourceManager), m_Pipeline(pipeline)
        {
        }

        void TraceRays(uint32_t width, uint32_t height, uint32_t depth)
        {
            vkCmdTraceRaysKHR(m_CommandBuffer, 
                &m_Pipeline.raygen_sbt.strided_device_address_region, 
                &m_Pipeline.miss_sbt.strided_device_address_region, 
                &m_Pipeline.hit_sbt.strided_device_address_region, 
                &m_Pipeline.call_sbt.strided_device_address_region, 
                width, height, depth);
        }

        template<typename T>
        void PushConstants(T &pushConstants)
        {
            // Assuming RT pipelines also support push constants via their layout
            // For now, assume it's at offset 0
            // vkCmdPushConstants(m_CommandBuffer, m_Pipeline.layout, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 0, sizeof(T), &pushConstants);
        }

    private:
        VkCommandBuffer m_CommandBuffer;
        ResourceManager &m_ResourceManager;
        RaytracingPipeline &m_Pipeline;
    };
}
