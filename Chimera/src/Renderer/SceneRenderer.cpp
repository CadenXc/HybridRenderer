#include "pch.h"
#include "SceneRenderer.h"
#include "Renderer/Backend/Renderer.h"
#include "Core/Application.h"
#include "Core/Layer.h"
#include "Utils/VulkanBarrier.h"
#include "Scene/Scene.h"

namespace Chimera {

    SceneRenderer::SceneRenderer(std::shared_ptr<VulkanContext> context, ResourceManager* resourceManager, std::shared_ptr<Renderer> renderer)
        : m_Context(context), m_ResourceManager(resourceManager), m_Renderer(renderer)
    {
    }

    void SceneRenderer::Render(Scene* scene, RenderPath* renderPath, const FrameContext& context, const std::vector<std::shared_ptr<Layer>>& layers)
    {
        if (!renderPath) return;

        try {
            VkCommandBuffer cmd = m_Renderer->BeginFrame();
            if (cmd == VK_NULL_HANDLE) return;

            uint32_t frameIdx = m_Renderer->GetCurrentFrameIndex();
            uint32_t imageIndex = m_Renderer->GetCurrentImageIndex();

            // 1. Update Global Resources
            UniformBufferObject ubo{};
            ubo.view = context.View;
            ubo.proj = context.Projection;
            ubo.prevView = m_LastView;
            ubo.prevProj = m_LastProj;
            ubo.cameraPos = glm::vec4(context.CameraPosition, 1.0f);
            ubo.lightPos = glm::vec4(5.0f, 5.0f, 5.0f, 1.0f); // TODO: From scene
            ubo.time = context.Time;
            ubo.frameCount = (int)context.FrameIndex;
            
            m_ResourceManager->UpdateGlobalResources(frameIdx, ubo);

            m_LastView = context.View;
            m_LastProj = context.Projection;

            // 2. Execute Render Path
            renderPath->Render(cmd, frameIdx, imageIndex, m_ResourceManager->GetGlobalDescriptorSet(frameIdx), m_Context->GetSwapChainImages(),
                [&](VkCommandBuffer uiCmd) {
                    
                    VkImage swapchainImage = m_Context->GetSwapChainImages()[imageIndex];
                    
                    // RenderGraph leaves the swapchain in COLOR_ATTACHMENT_OPTIMAL.
                    // We ensure the transition here for ImGui compatibility.
                    VulkanUtils::InsertImageBarrier(uiCmd, swapchainImage, 
                        VK_IMAGE_ASPECT_COLOR_BIT, 
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

                    Application::Get().BeginImGui();
                    
                    for (auto& layer : layers)
                        layer->OnUIRender();

                    Application::Get().EndImGui(uiCmd, m_Context->GetSwapChainImageViews()[imageIndex], m_Context->GetSwapChainExtent());

                    VulkanUtils::InsertImageBarrier(uiCmd, swapchainImage, 
                        VK_IMAGE_ASPECT_COLOR_BIT, 
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0);
                }
            );

            m_Renderer->EndFrame();
        } catch (const std::exception& e) {
            CH_CORE_ERROR("SceneRenderer EXCEPTION: {0}", e.what());
        }
    }

}
