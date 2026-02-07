#include "pch.h"
#include "RayTracedRenderPath.h"
#include "Renderer/Passes/RaytracePass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Resources/Buffer.h"
#include "Renderer/Backend/ShaderMetadata.h"
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
        RaytracePass raytrace(m_Scene, m_FrameCount);
        raytrace.Setup(graph);

        graph.AddBlitPass("Final Blit", RS::RT_OUTPUT, RS::RENDER_OUTPUT);

        graph.Build();
    }

    void RayTracedRenderPath::Update()
    {
        RenderPath::Update();

        static glm::mat4 lastView = glm::mat4(1.0f);
        if (m_Scene && m_Scene->GetCamera().view != lastView)
        {
            m_FrameCount = 0; 
            lastView = m_Scene->GetCamera().view;
        }

        m_FrameCount++;
        // We must rebuild the graph to update the frame index in the pass
        m_NeedsRebuild = true; 
    }

    void RayTracedRenderPath::OnSceneUpdated()
    {
        m_NeedsRebuild = true;
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
