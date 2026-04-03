#pragma once

#include "Renderer/Graph/RenderGraphCommon.h"
#include "Renderer/Graph/RenderGraph.h"
#include "IRenderGraphPass.h"
#include <memory>

namespace Chimera
{
    class Scene;

    struct RTAOPassData
    {
        RGResourceHandle normal;
        RGResourceHandle depth;
        RGResourceHandle output;
    };

    class RTAOPass : public RenderPass<RTAOPassData>
    {
    public:
        using PassData = RTAOPassData;
        static constexpr const char* Name = "RTAOPass";

        RTAOPass(std::shared_ptr<Scene> scene);

        virtual void Setup(PassData& data, RenderGraph::PassBuilder& builder) override;
        virtual void Execute(const PassData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd) override;

    private:
        std::shared_ptr<Scene> m_Scene;
    };
}
