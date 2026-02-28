#include "pch.h"
#include "RayQueryRenderPath.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Passes/RayQueryPass.h"
#include "Renderer/Passes/StandardPasses.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"
#include "Scene/Scene.h"
#include "Scene/Model.h"
#include "Core/Application.h"

namespace Chimera
{
    RayQueryRenderPath::RayQueryRenderPath(VulkanContext& context)
        : RenderPath(std::shared_ptr<VulkanContext>(&context, [](VulkanContext*){}))
    {
    }

    void RayQueryRenderPath::BuildGraph(RenderGraph& graph, std::shared_ptr<Scene> scene)
    {
        // 1. Ray Query Pass
        RayQueryPass::AddToGraph(graph, scene);

        // 2. Linearize Depth
        StandardPasses::AddLinearizeDepthPass(graph);
    }
}
