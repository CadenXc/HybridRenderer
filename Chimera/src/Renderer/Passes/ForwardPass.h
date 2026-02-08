#pragma once
#include "Renderer/Graph/RenderGraph.h"

namespace Chimera {

    class ForwardPass {
    public:
        ForwardPass(std::shared_ptr<class Scene> scene) : m_Scene(scene) {}
        void Setup(RenderGraph& graph);
        void SetScene(std::shared_ptr<class Scene> scene) { m_Scene = scene; }

    private:
        std::shared_ptr<class Scene> m_Scene;
    };

}