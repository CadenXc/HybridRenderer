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
#include "Renderer/Passes/ForwardPass.h"

#include "Core/Application.h"

#include <imgui.h>

namespace Chimera
{
RayTracedRenderPath::RayTracedRenderPath(VulkanContext& context)
    : RenderPath(context.GetShared())
{
}

void RayTracedRenderPath::BuildGraph(RenderGraph& graph,
                                     std::shared_ptr<Scene> scene)
{
    const bool canUseRayTracing = m_Context->IsRayTracingSupported() && scene && scene->GetTLAS() != VK_NULL_HANDLE;

    if (canUseRayTracing)
    {
		graph.AddPass<DepthPrepass>(scene);
		graph.AddPass<RaytracePass>(scene, m_UseAlphaTest);
    }
    else
    {
        graph.AddPass<ForwardPass>(scene);
    }

    const bool taaEnabled =
        Application::Get().GetFrameContext().RenderFlags & RenderFlags_TAABit;

    if (taaEnabled)
    {
        graph.AddPass<TAAPass>();
        graph.AddPass<PostProcessPass>("TAAOutput");
    }
    else
    {
        graph.AddPass<PostProcessPass>(RS::FinalColor);
    }
}

void RayTracedRenderPath::OnImGui()
{
    if (ImGui::TreeNodeEx("Ray Tracing Settings",
                          ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Checkbox("Use Alpha Testing", &m_UseAlphaTest);
        ImGui::TreePop();
    }
}
} // namespace Chimera
