#pragma once

#include "Renderer/Graph/RenderGraph.h"
#include "Scene/Scene.h"

namespace Chimera
{
    struct RTDiffuseGIData
    {
        RGResourceHandle normal;
        RGResourceHandle depth;
        RGResourceHandle material;
        RGResourceHandle output;
    };

    class RTDiffuseGIPass
    {
    public:
        static void AddToGraph(RenderGraph& graph, std::shared_ptr<Scene> scene);
    };
}
