#include "pch.h"
#include "ForwardRenderPath.h"
#include "Renderer/Passes/ForwardPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include <imgui.h>

namespace Chimera {

    ForwardRenderPath::ForwardRenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager, PipelineManager& pipelineManager, VkDescriptorSetLayout globalDescriptorSetLayout)
        : RenderPath(context, scene, resourceManager, pipelineManager), m_GlobalDescriptorSetLayout(globalDescriptorSetLayout)
    {
    }

    ForwardRenderPath::~ForwardRenderPath()
    {
        vkDeviceWaitIdle(m_Context->GetDevice());
    }

    void ForwardRenderPath::SetupGraph(RenderGraph& graph)
    {
        ForwardPass forward(m_Scene);
        forward.Setup(graph);

        // ‰ª?ForwardColor Êã∑Ë¥ùÂà?FinalColor
        graph.AddBlitPass("Forward Viewport Blit", RS::FORWARD_COLOR, RS::FINAL_COLOR);

        graph.Build();
    }

    void ForwardRenderPath::OnImGui()
    {
        ImGui::Text("Forward Rendering Path (RenderGraph)");
    }

}
