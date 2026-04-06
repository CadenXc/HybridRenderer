#pragma once

#include "Renderer/Graph/RenderGraphCommon.h"
#include "Renderer/Graph/RenderGraph.h"
#include "IRenderGraphPass.h"

namespace Chimera
{
struct SkyboxPassData
{
    RGResourceHandle output;
};

class SkyboxPass : public RenderPass<SkyboxPassData>
{
public:
    static constexpr const char* Name = "SkyboxPass";

    virtual void Setup(SkyboxPassData& data,
                       RenderGraph::PassBuilder& builder) override;
    virtual void Execute(const SkyboxPassData& data, RenderGraphRegistry& reg,
                         VkCommandBuffer cmd) override;
};
} // namespace Chimera
