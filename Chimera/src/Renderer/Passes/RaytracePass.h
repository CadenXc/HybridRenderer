#pragma once
#include "Renderer/Graph/RenderGraphCommon.h"

namespace Chimera
{
    class Scene;
    class RaytracePass
    {
    public:
        static void AddToGraph(RenderGraph& graph, std::shared_ptr<Scene> scene);
    };
}
