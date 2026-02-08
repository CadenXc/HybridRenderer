#pragma once
#include "volk.h"

namespace Chimera {

    class ComputeExecutionContext {
    public:
        ComputeExecutionContext(VkCommandBuffer cmd, struct RenderPass& pass, class RenderGraph& graph, class ResourceManager& rm, uint32_t resIdx) 
            : m_Cmd(cmd) {}
            
        void Dispatch(uint32_t x, uint32_t y, uint32_t z) { vkCmdDispatch(m_Cmd, x, y, z); }
        VkCommandBuffer GetCommandBuffer() const { return m_Cmd; }
    private:
        VkCommandBuffer m_Cmd;
    };

}