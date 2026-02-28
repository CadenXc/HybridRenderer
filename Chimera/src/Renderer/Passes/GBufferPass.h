#pragma once

#include "Renderer/Graph/RenderGraph.h"
#include "Scene/Scene.h"

namespace Chimera::GBufferPass
{
    /**
     * @brief 将 G-Buffer Pass 挂载到渲染图中
     */
    void AddToGraph(RenderGraph& graph, std::shared_ptr<Scene> scene);
}
