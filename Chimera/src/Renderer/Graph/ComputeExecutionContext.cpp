#include "pch.h"
#include "ComputeExecutionContext.h"
#include "RenderGraph.h"
#include "Core/Application.h"
#include "Renderer/RenderState.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/Backend/PipelineManager.h"

namespace Chimera
{
    void ComputeExecutionContext::Bind(const std::string& kernelName)
    {
        auto& pipelines = m_Graph.GetComputePipelines();
        if (pipelines.find(kernelName) == pipelines.end())
        {
            CH_CORE_ERROR("ComputeExecutionContext: Kernel {0} not found!", kernelName);
            return;
        }

        ComputePipeline* pipe = pipelines.at(kernelName);
        m_CurrentLayout = pipe->layout;
        vkCmdBindPipeline(m_Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe->handle);

        // Set 0: Global
        VkDescriptorSet globalSet = Application::Get().GetRenderState()->GetDescriptorSet(m_ResIdx);
        vkCmdBindDescriptorSets(m_Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe->layout, 0, 1, &globalSet, 0, nullptr);

        // Set 1: Scene
        VkDescriptorSet sceneSet = ResourceManager::Get().GetSceneDescriptorSet(m_ResIdx);
        vkCmdBindDescriptorSets(m_Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe->layout, 1, 1, &sceneSet, 0, nullptr);

        // Set 2: Pass
        if (m_Pass.descriptor_set != VK_NULL_HANDLE)
        {
            vkCmdBindDescriptorSets(m_Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe->layout, 2, 1, &m_Pass.descriptor_set, 0, nullptr);
        }
    }

    void ComputeExecutionContext::Dispatch(uint32_t x, uint32_t y, uint32_t z)
    {
        vkCmdDispatch(m_Cmd, x, y, z);
    }
}
