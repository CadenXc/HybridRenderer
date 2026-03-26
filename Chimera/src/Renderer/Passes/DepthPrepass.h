#pragma once

#include "Renderer/Graph/RenderGraphCommon.h"
#include "Renderer/Graph/RenderGraph.h"
#include "IRenderGraphPass.h"
#include <memory>

namespace Chimera
{
    class Scene;

    struct DepthPrepassData
    {
        RGResourceHandle depth;
    };

    class DepthPrepass : public RenderPass<DepthPrepassData>
    {
    public:
        using PassData = DepthPrepassData;
        static constexpr const char* Name = "DepthPrepass";

        using Data = PassData;

        DepthPrepass(std::shared_ptr<Scene> scene);

        virtual void Setup(PassData& data, RenderGraph::PassBuilder& builder) override;
        virtual void Execute(const PassData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd) override;

    private:
        std::shared_ptr<Scene> m_Scene;
    };
}
