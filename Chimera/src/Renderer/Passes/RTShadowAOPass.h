#pragma once

#include "Renderer/Graph/RenderGraph.h"
#include "Scene/Scene.h"

namespace Chimera
{
    struct RTShadowAOData
    {
        RGResourceHandle normal;
        RGResourceHandle depth;
        RGResourceHandle output;
    };

    class RTShadowAOPass
    {
    public:
        /**
         * @brief 将光追阴影与 AO Pass 挂载到渲染图中
         */
        static void AddToGraph(RenderGraph& graph, std::shared_ptr<Scene> scene);
    };
}
