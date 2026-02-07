#pragma once

#include "Renderer/Graph/RenderGraphPass.h"
#include "Scene/Scene.h"

namespace Chimera {

    class RaytracePass : public RenderGraphPass
    {
    public:
        RaytracePass(std::shared_ptr<Scene> scene, uint32_t& frameCount);
        virtual void Setup(RenderGraph& graph) override;

    private:
        RaytracingPipelineDescription CreatePipelineDescription();

    private:
        std::shared_ptr<Scene> m_Scene;
        uint32_t& m_FrameCount;
    };

}
