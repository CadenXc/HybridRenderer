#pragma once
#include "ExecutionContext.h"

namespace Chimera
{
    class RaytracingExecutionContext : public ExecutionContext
    {
    public:
        RaytracingExecutionContext(RenderGraph& graph, struct RenderGraphPass& pass, VkCommandBuffer cmd);
        virtual ~RaytracingExecutionContext() = default;

        // Support both direct shader name and full description
        void BindPipeline(const std::string& name);
        void BindPipeline(const struct RaytracingPipelineDescription& desc);
        
        void TraceRays(uint32_t w, uint32_t h, uint32_t d = 1);
    };
}
