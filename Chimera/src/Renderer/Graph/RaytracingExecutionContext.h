#pragma once
#include "volk.h"

namespace Chimera
{
    class RaytracingExecutionContext
    {
    public:
        RaytracingExecutionContext(VkCommandBuffer cmd, class VulkanContext& ctx, struct RaytracingPipeline& pipe) 
            : m_Cmd(cmd), m_Context(ctx), m_Pipe(pipe)
        {
        }
            
        void Dispatch(uint32_t w, uint32_t h, uint32_t d);

        VkCommandBuffer GetCommandBuffer() const
        {
            return m_Cmd;
        }

        struct RaytracingPipeline& GetPipeline()
        {
            return m_Pipe;
        }

    private:
        VkCommandBuffer m_Cmd;
        class VulkanContext& m_Context;
        struct RaytracingPipeline& m_Pipe;
    };
}
