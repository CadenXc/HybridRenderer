#pragma once
#include "pch.h"
#include "gfx/resources/ResourceManager.h"
#include "gfx/vulkan/VulkanCommon.h"

namespace Chimera
{
    class ComputeExecutionContext
    {
    public:
        ComputeExecutionContext(VkCommandBuffer commandBuffer, ResourceManager &resourceManager, ComputePipeline &pipeline)
            : m_CommandBuffer(commandBuffer), m_ResourceManager(resourceManager), m_Pipeline(pipeline)
        {
        }

        void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
        {
            vkCmdDispatch(m_CommandBuffer, groupCountX, groupCountY, groupCountZ);
        }

        template<typename T>
        void PushConstants(T &pushConstants)
        {
            vkCmdPushConstants(m_CommandBuffer, m_Pipeline.layout, m_Pipeline.push_constant_description.shader_stage, 0, m_Pipeline.push_constant_description.size, &pushConstants);
        }

    private:
        VkCommandBuffer m_CommandBuffer;
        ResourceManager &m_ResourceManager;
        ComputePipeline &m_Pipeline;
    };
}