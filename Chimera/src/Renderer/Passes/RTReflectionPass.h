#pragma once

#include "Renderer/Graph/RenderGraph.h"
#include "Scene/Scene.h"

namespace Chimera
{
    struct RTReflectionData
    {
        RGResourceHandle normal;
        RGResourceHandle depth;
        RGResourceHandle material;
        RGResourceHandle albedo;
        RGResourceHandle output;
    };

    class RTReflectionPass
    {
    public:
        static void AddToGraph(RenderGraph& graph, std::shared_ptr<Scene> scene);
    };
}
