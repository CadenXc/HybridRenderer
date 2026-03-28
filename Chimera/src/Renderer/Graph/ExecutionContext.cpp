#include "pch.h"
#include "ExecutionContext.h"
#include "Renderer/Graph/RenderGraphCommon.h"

namespace Chimera
{
    ExecutionContext::ExecutionContext(RenderGraph& graph, RenderGraphPass& pass, VkCommandBuffer cmd)
        : m_Graph(graph), m_Pass(pass), m_Cmd(cmd) 
    {
    }

    void ExecutionContext::PushConstants(VkShaderStageFlags stages, const void* data, uint32_t size)
    {
        if (m_ActiveLayout != VK_NULL_HANDLE)
        {
            vkCmdPushConstants(m_Cmd, m_ActiveLayout, stages, 0, size, data);
        }
    }
}
