#pragma once

#include "Renderer/Graph/RenderGraphCommon.h"
#include "Renderer/Graph/RenderGraph.h"
#include "IRenderGraphPass.h"
#include <memory>

namespace Chimera
{
    struct TAAPassData
    {
        RGResourceHandle current;
        RGResourceHandle motion;
        RGResourceHandle depth;
        RGResourceHandle history;
        RGResourceHandle output;
    };

    class TAAPass : public RenderPass<TAAPassData>
    {
    public:
        using PassData = TAAPassData;
        static constexpr const char* Name = "TAAPass";

        using Data = PassData;

        TAAPass();

        virtual void Setup(PassData& data, RenderGraph::PassBuilder& builder) override;
        virtual void Execute(const PassData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd) override;
    };
}
