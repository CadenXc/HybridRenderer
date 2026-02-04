#include "pch.h"
#include "HybridRenderPath.h"
#include "Renderer/Passes/GBufferPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include <imgui.h>

namespace Chimera
{
    HybridRenderPath::HybridRenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager, PipelineManager& pipelineManager, VkDescriptorSetLayout globalDescriptorSetLayout)
        : RenderPath(context, scene, resourceManager, pipelineManager), m_GlobalDescriptorSetLayout(globalDescriptorSetLayout)
    {
    }

    HybridRenderPath::~HybridRenderPath()
    {
        vkDeviceWaitIdle(m_Context->GetDevice());
    }

    void HybridRenderPath::SetupGraph(RenderGraph& graph)
    {
        GBufferPass gbuffer(m_Scene);
        gbuffer.Setup(graph);

        // 使用常量：从 ALBEDO 拷贝�?FINAL_COLOR
        graph.AddBlitPass("Viewport Blit", RS::ALBEDO, RS::FINAL_COLOR);

        graph.Build();
    }

    void HybridRenderPath::OnImGui()
    {
        ImGui::Text("Hybrid Rendering Path (RenderGraph)");
    }
}
