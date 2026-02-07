#pragma once

#include "Renderer/Graph/RenderGraphPass.h"
#include "Scene/Scene.h"

namespace Chimera {

    class GBufferPass : public RenderGraphPass
    {
    public:
        GBufferPass(std::shared_ptr<Scene> scene);
        virtual void Setup(RenderGraph& graph) override;

    private:
        GraphicsPipelineDescription CreatePipelineDescription();

    private:
        std::shared_ptr<Scene> m_Scene;
    };

}
