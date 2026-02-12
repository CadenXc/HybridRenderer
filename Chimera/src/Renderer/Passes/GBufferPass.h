#pragma once

#include "Renderer/Graph/RenderGraph.h"
#include "Scene/Scene.h"

namespace Chimera
{
    struct GBufferData
    {
        RGResourceHandle albedo;
        RGResourceHandle normal;
        RGResourceHandle material;
        RGResourceHandle motion;
        RGResourceHandle depth;
    };

    class GBufferPass
    {
    public:
        /**
         * @brief 将 G-Buffer Pass 挂载到渲染图中
         */
        static void AddToGraph(RenderGraph& graph, std::shared_ptr<Scene> scene);
    };
}
