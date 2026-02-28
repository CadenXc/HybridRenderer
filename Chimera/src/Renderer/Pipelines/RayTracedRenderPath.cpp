#include "pch.h"
#include "RayTracedRenderPath.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Passes/RaytracePass.h"
#include "Renderer/Passes/StandardPasses.h"
#include "Renderer/Graph/RaytracingExecutionContext.h"
#include <imgui.h>

namespace Chimera
{
    RayTracedRenderPath::RayTracedRenderPath(VulkanContext& context)
        : RenderPath(std::shared_ptr<VulkanContext>(&context, [](VulkanContext*){}))
    {
    }

    void RayTracedRenderPath::BuildGraph(RenderGraph& graph, std::shared_ptr<Scene> scene)
    {
        // 1. Full Path Tracing Pass
        RaytracePass::AddToGraph(graph, scene, m_UseAlphaTest);

        // 2. Linearize Depth (For debug view)
        StandardPasses::AddLinearizeDepthPass(graph);
    }

    void RayTracedRenderPath::OnImGui()
    {
        if (ImGui::TreeNodeEx("Ray Tracing Settings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Alpha Test for Shadows", &m_UseAlphaTest);
            ImGui::TreePop();
        }
    }
}
