#pragma once
#include "Renderer/Graph/RenderGraph.h"

namespace Chimera {

    class DeferredLightingPass {
    public:
        DeferredLightingPass(std::shared_ptr<class Scene> scene, uint32_t width, uint32_t height)
            : m_Scene(scene), m_Width(width), m_Height(height) {}

        void Setup(RenderGraph& graph);
        void SetScene(std::shared_ptr<class Scene> scene) { m_Scene = scene; }

    private:
        std::shared_ptr<class Scene> m_Scene;
        uint32_t m_Width, m_Height;
    };

}