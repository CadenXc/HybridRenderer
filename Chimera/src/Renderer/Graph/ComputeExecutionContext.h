#pragma once
#include "Renderer/Graph/RenderGraphCommon.h"

namespace Chimera
{
    class ComputeExecutionContext
    {
    public:
        ComputeExecutionContext(RenderGraph& graph, struct RenderPass& pass, VkCommandBuffer cmd);
        
        void BindPipeline(const std::string& shaderName);
        void Dispatch(const std::string& shaderName, uint32_t groupX, uint32_t groupY, uint32_t groupZ = 1);
        void PushConstants(VkShaderStageFlags stages, const void* data, uint32_t size);
        template<typename T> void PushConstants(VkShaderStageFlags stages, const T& data) { PushConstants(stages, &data, sizeof(T)); }

        VkCommandBuffer GetCommandBuffer() { return m_Cmd; }
        RenderGraph& GetGraph() { return m_Graph; }

    private:
        RenderGraph& m_Graph;
        struct RenderPass& m_Pass;
        VkCommandBuffer m_Cmd;
        VkPipelineLayout m_ActiveLayout = VK_NULL_HANDLE;
    };
}
