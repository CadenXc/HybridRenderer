#include "pch.h"
#include "RayQueryRenderPath.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Passes/RayQueryPass.h"
#include "Renderer/Passes/TAAPass.h"
#include "Renderer/Passes/PostProcessPass.h"
#include "Renderer/Passes/StandardPasses.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"
#include "Scene/Scene.h"
#include "Scene/Model.h"
#include "Core/Application.h"

namespace Chimera
{
    RayQueryRenderPath::RayQueryRenderPath(VulkanContext& context)
        : RenderPath(context.GetShared())
    {
    }

    void RayQueryRenderPath::BuildGraph(RenderGraph& graph, std::shared_ptr<Scene> scene)
    {
        // 1. Background Pass (Draws Skybox if exists)
        StandardPasses::AddSkyboxPass(graph);

        // 2. Ray Query Pass (Outputs RS::FinalColor, RS::Motion, RS::Depth)
        RayQueryPass::AddToGraph(graph, scene);

        // 3. Resolve Temporal Aliasing (Outputs TAAOutput)
        TAAPass::AddToGraph(graph);

        // 4. Final Composition & Tone Mapping (Outputs RS::RENDER_OUTPUT)
        PostProcessPass::AddToGraph(graph, "TAAOutput");

        // 5. Linearize Depth (For debug view)
        StandardPasses::AddLinearizeDepthPass(graph);
    }
}
