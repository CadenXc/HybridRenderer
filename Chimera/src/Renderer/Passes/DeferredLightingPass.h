#pragma once

#include "Renderer/Graph/RenderGraphPass.h"
#include "Scene/Scene.h"

namespace Chimera {

    class DeferredLightingPass : public RenderGraphPass
    {
    public:
        DeferredLightingPass();
        virtual void Setup(RenderGraph& graph) override;
    };

}
