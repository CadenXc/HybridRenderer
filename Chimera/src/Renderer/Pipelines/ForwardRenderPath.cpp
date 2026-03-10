#include "pch.h"
#include "ForwardRenderPath.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Passes/ForwardPass.h"
#include "Renderer/Passes/StandardPasses.h"
#include "Renderer/Passes/TAAPass.h"
#include "Renderer/Passes/PostProcessPass.h"
#include "Core/Application.h"

namespace Chimera
{
    ForwardRenderPath::ForwardRenderPath(VulkanContext& context)
        : RenderPath(context.GetShared())
    {
    }

    void ForwardRenderPath::BuildGraph(RenderGraph& graph, std::shared_ptr<Scene> scene)
    {
        // 1. Background Pass (Draws Skybox if exists)
        StandardPasses::AddSkyboxPass(graph);

        // 2. Scene Rendering (Outputs RS::FinalColor, uses depth testing)
        ForwardPass::AddToGraph(graph, scene);

        // 3. Resolve Temporal Aliasing (Outputs TAAOutput)
        TAAPass::AddToGraph(graph);

        // 4. Final Composition & Tone Mapping (Outputs RS::RENDER_OUTPUT)
        PostProcessPass::AddToGraph(graph, "TAAOutput");
    }
}
