#pragma once
#include "volk.h"

namespace Chimera {

    class RaytracingExecutionContext {
    public:
        RaytracingExecutionContext(VkCommandBuffer cmd, class VulkanContext& ctx, class ResourceManager& rm, struct RaytracingPipeline& pipe) 
            : m_Cmd(cmd) {}
            
        void Dispatch(uint32_t w, uint32_t h, uint32_t d);
        VkCommandBuffer GetCommandBuffer() const { return m_Cmd; }
    private:
        VkCommandBuffer m_Cmd;
    };

}