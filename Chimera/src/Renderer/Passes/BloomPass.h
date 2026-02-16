#pragma once

#include "Renderer/Graph/RenderGraph.h"

namespace Chimera
{
    class BloomPass
    {
    public:
        static void AddToGraph(RenderGraph& graph);
    };
}
