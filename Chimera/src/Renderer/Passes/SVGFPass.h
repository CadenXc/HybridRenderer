#pragma once
#include "Renderer/Graph/RenderGraphCommon.h"
#include <string>

namespace Chimera
{
    class Scene;

    namespace SVGFPass
    {
        // [NEW] Config Descriptor Pattern for better readability
        struct Config
        {
            std::string inputName = "CurColor";
            std::string prefix = "SVGF";
            std::string historyBaseName = "Accumulated";
            int atrousIterations = 5;
        };

        void AddToGraph(RenderGraph& graph, std::shared_ptr<Scene> scene, const Config& config);
    }
}
