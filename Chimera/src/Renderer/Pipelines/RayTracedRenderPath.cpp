#include "pch.h"
#include "RayTracedRenderPath.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Passes/RaytracePass.h"
#include "Renderer/Passes/DepthPrepass.h"
#include "Renderer/Passes/StandardPasses.h"
#include "Renderer/Passes/TAAPass.h"
#include "Renderer/Passes/PostProcessPass.h"
#include "Renderer/Graph/RaytracingExecutionContext.h"
#include <imgui.h>

namespace Chimera
{
    RayTracedRenderPath::RayTracedRenderPath(VulkanContext &context)
        : RenderPath(context.GetShared())
    {
    }

    void RayTracedRenderPath::BuildGraph(RenderGraph &graph, std::shared_ptr<Scene> scene)
    {
        // 0. Depth Prepass (MANDATORY for TAA)
        // TAA needs depth buffer for velocity dilation and reprojection.
        graph.AddPass<DepthPrepass>(scene);

        // 1. Ray Tracing Pass (Writes HDR output to FinalColor and Motion)
        graph.AddPass<RaytracePass>(scene, m_UseAlphaTest);

        // 2. TAA Pass (Stabilizes the jittered RT output)
        // Reads RS::FinalColor, RS::Motion, RS::Depth. Outputs "TAAOutput"
        graph.AddPass<TAAPass>();

        // 3. Post-Processing & Tone Mapping
        // Now reads from the stabilized "TAAOutput" instead of raw "FinalColor"
        graph.AddPass<PostProcessPass>("TAAOutput");
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
