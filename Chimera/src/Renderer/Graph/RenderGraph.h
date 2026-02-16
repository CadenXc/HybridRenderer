#pragma once

#include "Renderer/Graph/RenderGraphCommon.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"
#include "Renderer/Graph/RaytracingExecutionContext.h"
#include "Renderer/Graph/ComputeExecutionContext.h"

namespace Chimera
{
    template<typename PassData>
    void RenderGraph::AddComputePass(const std::string& name, 
                       std::function<void(PassData&, PassBuilder&)> setup, 
                       std::function<void(const PassData&, ComputeExecutionContext&)> execute)
    {
        auto& pass = m_PassStack.emplace_back();
        pass.name = name;
        pass.isCompute = true;
        auto data = std::make_shared<PassData>();
        PassBuilder builder(*this, pass);
        setup(*data, builder);
        pass.executeFunc = [=](RenderGraphRegistry& reg, VkCommandBuffer cmd) { 
            ComputeExecutionContext ctx(reg.graph, reg.pass, cmd);
            execute(*data, ctx); 
        };
    }
}
