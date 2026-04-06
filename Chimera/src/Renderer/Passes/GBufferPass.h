#pragma once

#include "Renderer/Graph/RenderGraphCommon.h"
#include "Renderer/Graph/RenderGraph.h"
#include "IRenderGraphPass.h"
#include <memory>

namespace Chimera
{
class Scene;

struct GBufferPassData
{
    RGResourceHandle albedo;
    RGResourceHandle normal;
    RGResourceHandle material;
    RGResourceHandle motion;
    RGResourceHandle emissive;
    RGResourceHandle depth;
};

class GBufferPass : public RenderPass<GBufferPassData>
{
public:
    using PassData = GBufferPassData;
    static constexpr const char* Name = "GBufferPass";

    using Data = PassData;

    GBufferPass(std::shared_ptr<Scene> scene);

    virtual void Setup(PassData& data,
                       RenderGraph::PassBuilder& builder) override;
    virtual void Execute(const PassData& data, RenderGraphRegistry& reg,
                         VkCommandBuffer cmd) override;

private:
    std::shared_ptr<Scene> m_Scene;
};
} // namespace Chimera
