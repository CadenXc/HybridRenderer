#pragma once
#include "Renderer/Graph/RenderGraphCommon.h"
#include <memory>

namespace Chimera
{
    class Scene;

    namespace RTDiffuseGIPass
    {
        void AddToGraph(RenderGraph& graph, std::shared_ptr<Scene> scene);
    }
}
