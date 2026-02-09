#pragma once
#include "volk.h"
#include <glm/glm.hpp>

namespace Chimera
{
    class GraphicsExecutionContext
    {
    public:
        GraphicsExecutionContext(VkCommandBuffer cmd, VkPipelineLayout layout) : m_Cmd(cmd), m_Layout(layout)
        {
        }
        
        void DrawFullscreenQuad()
        {
            vkCmdDraw(m_Cmd, 3, 1, 0, 0);
        }

        void DrawIndexed(uint32_t c, uint32_t inst, uint32_t first, int32_t offset, uint32_t firstInst)
        { 
            vkCmdDrawIndexed(m_Cmd, c, inst, first, offset, firstInst); 
        }
        
        VkCommandBuffer GetCommandBuffer() const
        {
            return m_Cmd;
        }
        
        template<typename T> 
        void PushConstants(const T& data, uint32_t offset = 0)
        {
            vkCmdPushConstants(m_Cmd, m_Layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, offset, sizeof(T), &data);
        }

    private:
        VkCommandBuffer m_Cmd;
        VkPipelineLayout m_Layout;
    };
}
