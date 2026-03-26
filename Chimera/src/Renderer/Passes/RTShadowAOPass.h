#pragma once

#include "Renderer/Graph/RenderGraphCommon.h"
#include "Renderer/Graph/RenderGraph.h"
#include "IRenderGraphPass.h"
#include <memory>

namespace Chimera
{
    class Scene;

    struct RTShadowAOPassData
    {
        RGResourceHandle normal;
        RGResourceHandle depth;
        RGResourceHandle output;
    };

    class RTShadowAOPass : public RenderPass<RTShadowAOPassData>
    {
    public:
        using PassData = RTShadowAOPassData;
        static constexpr const char* Name = "RTShadowAOPass";

        using Data = PassData;

        RTShadowAOPass(std::shared_ptr<Scene> scene);

        virtual void Setup(PassData& data, RenderGraph::PassBuilder& builder) override;
        virtual void Execute(const PassData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd) override;

    private:
        std::shared_ptr<Scene> m_Scene;
    };
}
