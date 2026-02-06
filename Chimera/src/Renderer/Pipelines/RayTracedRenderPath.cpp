#include "pch.h"
#include "RayTracedRenderPath.h"
#include "Renderer/Passes/RaytracePass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Resources/Buffer.h"
#include <imgui.h>

namespace Chimera
{
    RayTracedRenderPath::RayTracedRenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager, PipelineManager& pipelineManager, VkDescriptorSetLayout globalDescriptorSetLayout)
        : RenderPath(context, scene, resourceManager, pipelineManager), m_GlobalDescriptorSetLayout(globalDescriptorSetLayout)
    {
    }

    RayTracedRenderPath::~RayTracedRenderPath()
    {
        vkDeviceWaitIdle(m_Context->GetDevice());
    }

    void RayTracedRenderPath::SetupGraph(RenderGraph& graph)
    {
        // RaytracePass now gets its buffers directly from m_Scene
        RaytracePass raytrace(m_Scene, m_FrameCount);
        raytrace.Setup(graph);

        graph.AddBlitPass("RT Viewport Blit", RS::RT_OUTPUT, RS::FINAL_COLOR);
        graph.Build();
    }

    void RayTracedRenderPath::OnSceneUpdated()
    {
        RenderPath::OnSceneUpdated();
        m_FrameCount = 0;
    }

    void RayTracedRenderPath::SetScene(std::shared_ptr<Scene> scene)
    {
        m_Scene = scene;
        OnSceneUpdated();
    }

    void RayTracedRenderPath::OnImGui()
    {
        ImGui::Text("Ray Tracing Path (RenderGraph)");
        ImGui::Text("Accumulated Frames: %d", m_FrameCount);
        if (ImGui::Button("Reset Accumulation")) m_FrameCount = 0;
    }
}
