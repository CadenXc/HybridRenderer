#pragma once

#include "RenderGraphCommon.h"
#include <string>
#include <vector>

namespace Chimera
{
    class RenderGraph;
    struct RenderPass;
    class Shader;

    /**
     * @brief Granite-style Execution Context that automates pipeline fetching and binding.
     */
    class ComputeExecutionContext
    {
    public:
        ComputeExecutionContext(RenderGraph& graph, RenderPass& pass, VkCommandBuffer cmd);

        // [AUTOMATED] 自动处理一切绑定并执行光追
        void DispatchRays(const RaytracingPipelineDescription& desc);

        // [AUTOMATED] 自动处理一切绑定并执行计算
        void Dispatch(const std::string& shaderName, uint32_t groupX, uint32_t groupY, uint32_t groupZ);

    private:
        // [FIX] 修正签名，匹配实现
        void BindAutomaticSets(VkPipelineBindPoint bindPoint, const std::vector<const Shader*>& shaders, VkPipelineLayout layout);

    private:
        RenderGraph& m_Graph;
        RenderPass& m_Pass;
        VkCommandBuffer m_Cmd;
    };
}
