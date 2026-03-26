#pragma once

#include "Renderer/Graph/RenderGraphCommon.h"
#include "Renderer/Graph/RenderGraph.h"
#include "IRenderGraphPass.h"
#include <memory>

namespace Chimera
{
    class Scene;

    struct RaytracePassData
    {
        RGResourceHandle output;
    };

    class RaytracePass : public RenderPass<RaytracePassData>
    {
    public:
        static constexpr const char* Name = "RaytracePass";

        RaytracePass(std::shared_ptr<Scene> scene, bool useAlphaTest);

        virtual void Setup(RaytracePassData& data, RenderGraph::PassBuilder& builder) override;
        virtual void Execute(const RaytracePassData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd) override;

    private:
        std::shared_ptr<Scene> m_Scene;
        bool m_UseAlphaTest;
    };
}
