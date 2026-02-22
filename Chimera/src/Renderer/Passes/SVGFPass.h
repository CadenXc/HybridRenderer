#pragma once
#include "Renderer/Graph/RenderGraphCommon.h"

namespace Chimera
{
    class Scene;

    class SVGFPass
    {
    public:
        static void AddToGraph(RenderGraph& graph, std::shared_ptr<Scene> scene,
                             const std::string& inputName = "CurColor",
                             const std::string& outputPrefix = "SVGF",
                             const std::string& historyPrefix = "Accumulated");
    };
}
