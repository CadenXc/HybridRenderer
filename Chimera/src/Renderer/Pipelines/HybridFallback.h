#pragma once

#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Passes/StandardPasses.h"

namespace Chimera
{
inline void AddHybridRayTracingFallbacks(RenderGraph& graph)
{
    const VkClearColorValue fullyVisible = {{1.0f, 1.0f, 0.0f, 0.0f}};
    const VkClearColorValue black = {{0.0f, 0.0f, 0.0f, 0.0f}};

    StandardPasses::AddClearPass(graph, RS::ShadowAO, fullyVisible);
    StandardPasses::AddClearPass(graph, "ReflectionRaw", black);
    StandardPasses::AddClearPass(graph, "GIRaw", black);
}
} // namespace Chimera
