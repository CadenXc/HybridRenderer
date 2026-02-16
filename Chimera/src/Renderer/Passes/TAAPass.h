#pragma once

#include "Renderer/Graph/RenderGraph.h"

namespace Chimera
{
    struct TAAData
    {
        RGResourceHandle current;
        RGResourceHandle history;
        RGResourceHandle motion;
        RGResourceHandle depth;
        RGResourceHandle bloom;
        RGResourceHandle output;
    };

    class TAAPass
    {
    public:
        static void AddToGraph(RenderGraph& graph);
    };
}
