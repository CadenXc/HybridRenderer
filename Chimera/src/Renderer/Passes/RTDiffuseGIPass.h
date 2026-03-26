#pragma once

#include "Renderer/Graph/RenderGraphCommon.h"
#include "Renderer/Graph/RenderGraph.h"
#include "IRenderGraphPass.h"
#include <memory>

namespace Chimera
{
    class Scene;

    struct RTDiffuseGIPassData
    {
        RGResourceHandle normal;
        RGResourceHandle depth;
        RGResourceHandle material;
        RGResourceHandle output;
    };

    class RTDiffuseGIPass : public RenderPass<RTDiffuseGIPassData>
    {
    public:
        using PassData = RTDiffuseGIPassData;
        static constexpr const char* Name = "RTDiffuseGIPass";

        using Data = PassData;

        RTDiffuseGIPass(std::shared_ptr<Scene> scene);

        virtual void Setup(PassData& data, RenderGraph::PassBuilder& builder) override;
        virtual void Execute(const PassData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd) override;

    private:
        std::shared_ptr<Scene> m_Scene;
    };
}
