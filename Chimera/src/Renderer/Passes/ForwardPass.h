#pragma once
#include "Renderer/Graph/RenderGraphCommon.h"

namespace Chimera
{
    class Scene;
    class ForwardPass
    {
    public:
        static void AddToGraph(RenderGraph& graph, std::shared_ptr<Scene> scene);
    };
}
