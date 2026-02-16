#pragma once
#include "Renderer/Graph/RenderGraphCommon.h"

namespace Chimera
{
    class RaytracingExecutionContext
    {
    public:
        RaytracingExecutionContext(RenderGraph& graph, struct RenderPass& pass, VkCommandBuffer cmd);
        
        void BindPipeline(const struct RaytracingPipelineDescription& desc);
        void TraceRays(uint32_t width, uint32_t height, uint32_t depth = 1);
        
        template<typename T>
        void PushConstants(VkShaderStageFlags stages, const T& data)
        {
            if (m_ActiveLayout != VK_NULL_HANDLE)
            {
                vkCmdPushConstants(m_Cmd, m_ActiveLayout, VK_SHADER_STAGE_ALL, 0, sizeof(T), &data);
            }
        }

        RenderGraph& GetGraph() { return m_Graph; }
        VkCommandBuffer GetCommandBuffer() { return m_Cmd; }

    private:
        RenderGraph& m_Graph;
        struct RenderPass& m_Pass;
        VkCommandBuffer m_Cmd;
        VkPipelineLayout m_ActiveLayout = VK_NULL_HANDLE;
    };
}
