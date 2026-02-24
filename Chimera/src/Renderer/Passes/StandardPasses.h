#pragma once
#include "Renderer/Graph/RenderGraph.h"
#include <string>

namespace Chimera
{
    class StandardPasses
    {
    public:
        // 自动添加深度线性化 Pass 并更新 debug_view
        static void AddLinearizeDepthPass(RenderGraph& graph);
    };
}
