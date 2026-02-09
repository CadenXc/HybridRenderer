#pragma once
#include "Renderer/Graph/RenderGraph.h"

namespace Chimera {

    class LinearizeDepthPass {
    public:
        LinearizeDepthPass() = default;
        void Setup(RenderGraph& graph);
    };

}
