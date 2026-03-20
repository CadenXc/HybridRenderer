#pragma once
#include <memory>

namespace Chimera
{
    class RenderGraph;
    class Scene;

    namespace DepthPrepass
    {
        void AddToGraph(RenderGraph& graph, std::shared_ptr<Scene> scene);
    }
}
