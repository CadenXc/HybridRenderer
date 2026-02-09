#pragma once
#include "volk.h"
#include <string>

namespace Chimera
{
    class ComputeExecutionContext
    {
    public:
        ComputeExecutionContext(VkCommandBuffer cmd, struct RenderPass& pass, class RenderGraph& graph, uint32_t resIdx) 
            : m_Cmd(cmd), m_Pass(pass), m_Graph(graph), m_ResIdx(resIdx)
        {
        }
            
        void Bind(const std::string& kernelName);
        void Dispatch(uint32_t x, uint32_t y, uint32_t z);
        
        template<typename T>
        void PushConstants(const T& data, uint32_t offset = 0)
        {
            if (m_CurrentLayout != VK_NULL_HANDLE)
            {
                vkCmdPushConstants(m_Cmd, m_CurrentLayout, VK_SHADER_STAGE_COMPUTE_BIT, offset, sizeof(T), &data);
            }
        }

        VkCommandBuffer GetCommandBuffer() const
        {
            return m_Cmd;
        }

    private:
        VkCommandBuffer m_Cmd;
        struct RenderPass& m_Pass;
        class RenderGraph& m_Graph;
        uint32_t m_ResIdx;
        VkPipelineLayout m_CurrentLayout = VK_NULL_HANDLE;
    };
}
