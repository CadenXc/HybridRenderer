#include "pch.h"
#include "SceneRenderer.h"
#include "Renderer/Backend/Renderer.h"
#include "Core/Application.h"
#include "Core/Layer.h"
#include "Utils/VulkanBarrier.h"
#include "Scene/Scene.h"
#include "Core/EngineConfig.h"
#include "Core/ImGuiLayer.h"
#include "Renderer/RenderState.h"

namespace Chimera {

    SceneRenderer::SceneRenderer(std::shared_ptr<VulkanContext> context, ResourceManager* resourceManager, std::shared_ptr<Renderer> renderer, std::shared_ptr<ImGuiLayer> imguiLayer)
        : m_Context(context), m_ResourceManager(resourceManager), m_Renderer(renderer), m_ImGuiLayer(imguiLayer)
    {
    }

    void SceneRenderer::Render(Scene* scene, RenderPath* renderPath, const FrameContext& context, const std::vector<std::shared_ptr<Layer>>& layers)
    {
        if (!renderPath) return;
        if (context.ViewportSize.x <= 0.0f || context.ViewportSize.y <= 0.0f) return;

        renderPath->Update();

        try {
            VkCommandBuffer cmd = m_Renderer->BeginFrame();
            if (cmd == VK_NULL_HANDLE) return;

            uint32_t frameIdx = m_Renderer->GetCurrentFrameIndex();
            uint32_t imageIndex = m_Renderer->GetCurrentImageIndex();

            GlobalFrameData frameData{};
            frameData.view = context.View;
            frameData.proj = context.Projection;
            frameData.viewInverse = glm::inverse(context.View);
            frameData.projInverse = glm::inverse(context.Projection);
            frameData.viewProjInverse = glm::inverse(context.Projection * context.View);
            frameData.prevView = m_LastView;
            frameData.prevProj = m_LastProj;
            
            frameData.directionalLight.direction = glm::vec4(glm::normalize(glm::vec3(Config::Settings.LightPosition[0], Config::Settings.LightPosition[1], Config::Settings.LightPosition[2])), 0.0f);
            frameData.directionalLight.color = glm::vec4(Config::Settings.LightColor[0], Config::Settings.LightColor[1], Config::Settings.LightColor[2], 1.0f);
            frameData.directionalLight.intensity = glm::vec4(Config::Settings.LightIntensity);
            
            frameData.displaySize = context.ViewportSize;
            frameData.displaySizeInverse = 1.0f / context.ViewportSize;
            frameData.frameIndex = context.FrameIndex;
            frameData.frameCount = renderPath->GetFrameCount(); 
            frameData.displayMode = (uint32_t)Config::Settings.DisplayMode;
            frameData.cameraPos = glm::vec4(context.CameraPosition, 1.0f);
            
            Application::Get().GetRenderState()->Update(frameIdx, frameData);

            m_LastView = context.View;
            m_LastProj = context.Projection;

            // 2. Execute Render Path
            renderPath->Render(cmd, frameIdx, imageIndex, Application::Get().GetRenderState()->GetDescriptorSet(frameIdx), m_Context->GetSwapChainImages(),
                [&](VkCommandBuffer uiCmd) {
                    m_ImGuiLayer->Begin();
                    for (auto& layer : layers)
                        layer->OnUIRender();
                    m_ImGuiLayer->End(uiCmd);
                }
            );

            m_Renderer->EndFrame();
        } catch (const std::exception& e) {
            CH_CORE_ERROR("SceneRenderer EXCEPTION: {0}", e.what());
            m_Renderer->ResetFrameState();
            if (std::string(e.what()).find("DEVICE_LOST") != std::string::npos) {
                CH_CORE_FATAL("CRITICAL: Vulkan Device Lost.");
                Application::Get().Close();
            }
        }
    }

}