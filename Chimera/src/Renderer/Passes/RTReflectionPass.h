#pragma once

#include "Renderer/Graph/RenderGraphCommon.h"
#include "Renderer/Graph/RenderGraph.h"
#include "IRenderGraphPass.h"
#include <memory>

namespace Chimera
{
class Scene;

struct RTReflectionPassData
{
    RGResourceHandle normal;
    RGResourceHandle depth;
    RGResourceHandle material;
    RGResourceHandle albedo;
    RGResourceHandle output;
};

class RTReflectionPass : public RenderPass<RTReflectionPassData>
{
public:
    using PassData = RTReflectionPassData;
    static constexpr const char* Name = "RTReflectionPass";

    using Data = PassData;

    RTReflectionPass(std::shared_ptr<Scene> scene);

    virtual void Setup(PassData& data,
                       RenderGraph::PassBuilder& builder) override;
    virtual void Execute(const PassData& data, RenderGraphRegistry& reg,
                         VkCommandBuffer cmd) override;

private:
    std::shared_ptr<Scene> m_Scene;
};
} // namespace Chimera
