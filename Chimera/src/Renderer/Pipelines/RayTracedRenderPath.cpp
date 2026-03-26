#include "pch.h"
#include "RayTracedRenderPath.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Passes/RaytracePass.h"
#include "Renderer/Passes/StandardPasses.h"
#include "Renderer/Passes/TAAPass.h"
#include "Renderer/Passes/PostProcessPass.h"
#include "Renderer/Graph/RaytracingExecutionContext.h"
#include <imgui.h>

namespace Chimera
{
    RayTracedRenderPath::RayTracedRenderPath(VulkanContext& context)
        : RenderPath(context.GetShared())
    {
    }

    void RayTracedRenderPath::BuildGraph(RenderGraph& graph, std::shared_ptr<Scene> scene)
    {
        // 1. Full Path Tracing Pass (Outputs RS::FinalColor and RS::Motion)
        graph.AddPass<RaytracePass>(scene, m_UseAlphaTest);

        // 2. TAA Pass (Stabilizes the jittered RT output, outputs "TAAOutput")
        graph.AddPass<TAAPass>();

        // 3. Final Composition & Tone Mapping (Outputs RS::RENDER_OUTPUT)
        graph.AddPass<PostProcessPass>("TAAOutput");

        // 4. Linearize Depth (For debug view)
        StandardPasses::AddLinearizeDepthPass(graph);
    }

    void RayTracedRenderPath::OnImGui()
    {
        if (ImGui::TreeNodeEx("Ray Tracing Settings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Use Alpha Testing", &m_UseAlphaTest);
            ImGui::TreePop();
        }
    }
}

