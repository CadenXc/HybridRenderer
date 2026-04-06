#pragma once
#include "ExecutionContext.h"

namespace Chimera
{
class ComputeExecutionContext : public ExecutionContext
{
public:
    ComputeExecutionContext(RenderGraph& graph, RenderGraphPass& pass,
                            VkCommandBuffer cmd);

    void BindPipeline(const std::string& shaderName);
    void Dispatch(const std::string& shaderName, uint32_t groupX,
                  uint32_t groupY, uint32_t groupZ = 1);
};
} // namespace Chimera
