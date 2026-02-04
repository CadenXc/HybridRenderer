#pragma once

#include "Renderer/Graph/RenderGraphPass.h"
#include "Scene/Scene.h"

namespace Chimera {

    class ForwardPass : public RenderGraphPass
    {
    public:
        ForwardPass(std::shared_ptr<Scene> scene);
        virtual void Setup(RenderGraph& graph) override;

    private:
        std::shared_ptr<Scene> m_Scene;
    };

}
