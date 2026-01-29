#pragma once
#include "rendering/graph/core/RenderGraph.h"

namespace Chimera {

    class IRenderGraphPass {
    public:
        virtual ~IRenderGraphPass() = default;

        // 将此 Pass 的逻辑（资源定义、Pipeline、回调）添加到 Graph 中
        virtual void AddToGraph(RenderGraph& graph, uint32_t width, uint32_t height) = 0;
    };

}
