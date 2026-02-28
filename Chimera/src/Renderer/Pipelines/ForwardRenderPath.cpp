#include "pch.h"
#include "ForwardRenderPath.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Passes/ForwardPass.h"
#include "Renderer/Passes/StandardPasses.h"

namespace Chimera
{
    ForwardRenderPath::ForwardRenderPath(VulkanContext& context)
        : RenderPath(std::shared_ptr<VulkanContext>(&context, [](VulkanContext*){}))
    {
    }

    void ForwardRenderPath::BuildGraph(RenderGraph& graph, std::shared_ptr<Scene> scene)
    {
        // 1. Main Forward Pass
        ForwardPass::AddToGraph(graph, scene);

        // 2. Linearize Depth (For debug visualization)
        StandardPasses::AddLinearizeDepthPass(graph);
    }
}
