#pragma once

#include "Renderer/Graph/RenderGraphCommon.h"
#include "Renderer/Graph/RenderGraph.h"
#include "IRenderGraphPass.h"
#include <memory>

namespace Chimera
{
class Scene;

struct ForwardPassData
{
    RGResourceHandle color;
    RGResourceHandle depth;
};

class ForwardPass : public RenderPass<ForwardPassData>
{
public:
    static constexpr const char* Name = "ForwardPass";

    ForwardPass(std::shared_ptr<Scene> scene);

    virtual void Setup(ForwardPassData& data,
                       RenderGraph::PassBuilder& builder) override;
    virtual void Execute(const ForwardPassData& data, RenderGraphRegistry& reg,
                         VkCommandBuffer cmd) override;

private:
    std::shared_ptr<Scene> m_Scene;
};
} // namespace Chimera
