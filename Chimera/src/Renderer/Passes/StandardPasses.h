#pragma once
#include "Renderer/Graph/RenderGraphCommon.h"

namespace Chimera::StandardPasses
{
    void AddLinearizeDepthPass(RenderGraph& graph);
    void AddClearPass(RenderGraph& graph, const std::string& name, const VkClearColorValue& clearColor);
    void AddSkyboxPass(RenderGraph& graph);
}
