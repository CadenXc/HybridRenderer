#include "pch.h"
#include "Application.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Backend/Renderer.h"
#include "Renderer/Backend/PipelineManager.h"
#include "Renderer/Pipelines/ForwardRenderPath.h"
#include "Renderer/Pipelines/HybridRenderPath.h"
#include "Renderer/Pipelines/RayTracedRenderPath.h"
#include "Core/Events/ApplicationEvent.h"
#include "Assets/AssetImporter.h"
#include "Core/Input.h"
#include "Core/Window.h"
#include "Core/ImGuiLayer.h"
#include "Core/Layer.h"
#include "Utils/VulkanBarrier.h"
#include "Core/EngineConfig.h"
#include "Renderer/Graph/ResourceNames.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <cstdlib>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace Chimera {

    Application* Application::s_Instance = nullptr;

    Application::Application(const ApplicationSpecification& spec)
        : m_Specification(spec)
    {
        s_Instance = this;
        WindowProps props(spec.Name, spec.Width, spec.Height);
        m_Window = Window::Create(props);
        m_Window->SetEventCallback(BIND_EVENT_FN(Application::OnEvent));

        m_Context = std::make_shared<VulkanContext>(m_Window->GetNativeWindow());

        m_ResourceManager = std::make_unique<ResourceManager>(m_Context);
        m_ResourceManager->InitGlobalResources();

        m_PipelineManager = std::make_unique<PipelineManager>(m_Context, *m_ResourceManager);

        m_Renderer = std::make_unique<Renderer>(m_Context);

        m_ImGuiLayer = std::make_unique<ImGuiLayer>(m_Context);
        m_ImGuiLayer->OnAttach();

        m_Scene = std::make_shared<Scene>(m_Context, m_ResourceManager.get());

        m_RenderPath = std::make_unique<HybridRenderPath>(m_Context, m_Scene, m_ResourceManager.get(), *m_PipelineManager, m_ResourceManager->GetGlobalDescriptorSetLayout());
        m_RenderPath->Init();

        m_LastFrameTime = (float)glfwGetTime();
    }

    Application::~Application()
    {
        cleanup();
    }

    void Application::OnEvent(Event& e)
    {
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<WindowCloseEvent>(BIND_EVENT_FN(Application::OnWindowClose));
        dispatcher.Dispatch<WindowResizeEvent>(BIND_EVENT_FN(Application::OnWindowResize));

        m_ImGuiLayer->OnEvent(e);
        if (e.Handled) return;

        for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
        {
            if (e.Handled) break;
            (*it)->OnEvent(e);
        }
    }

    void Application::Run()
    {
        CH_CORE_INFO("Main loop started.");
        while (!glfwWindowShouldClose(m_Window->GetNativeWindow()))
        {
            float time = (float)glfwGetTime();
            Timestep timestep = time - m_LastFrameTime;
            m_LastFrameTime = time;

            m_Window->OnUpdate();

            if (m_RenderPathSwitchPending)
            {
                ExecuteRenderPathSwitch(m_PendingRenderPathType);
                m_RenderPathSwitchPending = false;
            }

            if (m_SceneLoadPending)
            {
                ExecuteLoadScene(m_PendingScenePath);
                m_SceneLoadPending = false;
            }

            if (m_ShaderReloadPending)
            {
                vkDeviceWaitIdle(m_Context->GetDevice());
                m_PipelineManager->ClearCache();
                m_RenderPath->OnSceneUpdated();
                m_ShaderReloadPending = false;
                CH_CORE_INFO("Manual shader reload executed.");
            }

            // Camera is now updated via Layer::OnUpdate (e.g. EditorLayer)
            for (auto& layer : m_LayerStack)
                layer->OnUpdate(timestep);
            
            // 热重载逻辑使用 Config 设置
            static float shaderCheckTimer = 0.0f;
            shaderCheckTimer += timestep.GetSeconds();
            if (Config::Settings.EnableHotReload && shaderCheckTimer > Config::Settings.HotReloadCheckInterval)
            {
                if (m_PipelineManager->CheckForSourceUpdates())
                {
                    CH_CORE_INFO("Auto-recompiling shaders...");
                    RecompileShaders();
                }
                if (m_PipelineManager->CheckForShaderUpdates()) m_RenderPath->OnSceneUpdated();
                shaderCheckTimer = 0.0f;
            }

            drawFrame();
        }
        CH_CORE_INFO("Main loop ended.");
    }

    void Application::RecompileShaders()
    {
        CH_CORE_INFO("Recompiling shaders...");
        int res = std::system("cmake --build ../.. --target UpdateShaders");
        if (res == 0) m_ShaderReloadPending = true;
        else CH_CORE_ERROR("Failed to recompile shaders.");
    }

    void Application::drawFrame()
    {
        try {
            VkCommandBuffer cmd = m_Renderer->BeginFrame();
            if (cmd == VK_NULL_HANDLE) return;

            uint32_t frameIdx = m_Renderer->GetCurrentFrameIndex();
            uint32_t imageIndex = m_Renderer->GetCurrentImageIndex();

            static glm::mat4 lastView = m_FrameContext.View;
            static glm::mat4 lastProj = m_FrameContext.Projection;

            UniformBufferObject ubo{};
            ubo.view = m_FrameContext.View;
            ubo.proj = m_FrameContext.Projection;
            ubo.prevView = lastView;
            ubo.prevProj = lastProj;
            ubo.cameraPos = glm::vec4(m_FrameContext.CameraPosition, 1.0f);
            ubo.lightPos = glm::vec4(5.0f, 5.0f, 5.0f, 1.0f);
            ubo.time = m_FrameContext.Time;
            ubo.frameCount = (int)m_FrameContext.FrameIndex;
            m_ResourceManager->UpdateGlobalResources(frameIdx, ubo);

            lastView = m_FrameContext.View;
            lastProj = m_FrameContext.Projection;

            m_RenderPath->Render(cmd, frameIdx, imageIndex, m_ResourceManager->GetGlobalDescriptorSet(frameIdx), m_Context->GetSwapChainImages(),
                [&](VkCommandBuffer uiCmd) {
                    VkImage swapchainImage = m_Context->GetSwapChainImages()[imageIndex];
                    
                    // RenderGraph leaves the swapchain in COLOR_ATTACHMENT_OPTIMAL.
                    // We ensure the transition here for ImGui compatibility.
                    VulkanUtils::InsertImageBarrier(uiCmd, swapchainImage, 
                        VK_IMAGE_ASPECT_COLOR_BIT, 
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

                    m_ImGuiLayer->Begin();
                    
                    for (auto& layer : m_LayerStack)
                        layer->OnUIRender();

                    m_ImGuiLayer->End(uiCmd, m_Context->GetSwapChainImageViews()[imageIndex], m_Context->GetSwapChainExtent());

                    VulkanUtils::InsertImageBarrier(uiCmd, swapchainImage, 
                        VK_IMAGE_ASPECT_COLOR_BIT, 
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0);
                }
            );

            m_Renderer->EndFrame();
        } catch (const std::exception& e) {
            CH_CORE_ERROR("EXCEPTION in drawFrame: {0}", e.what());
        }
    }

    void Application::ExecuteRenderPathSwitch(RenderPathType type)
    {
        CH_CORE_INFO("Switching Render Path to: {0}", (int)type);
        vkDeviceWaitIdle(m_Context->GetDevice());
        m_RenderPath.reset(); 
        try {
            if (type == RenderPathType::Forward) m_RenderPath = std::make_unique<ForwardRenderPath>(m_Context, m_Scene, m_ResourceManager.get(), *m_PipelineManager, m_ResourceManager->GetGlobalDescriptorSetLayout());
            else if (type == RenderPathType::Hybrid) m_RenderPath = std::make_unique<HybridRenderPath>(m_Context, m_Scene, m_ResourceManager.get(), *m_PipelineManager, m_ResourceManager->GetGlobalDescriptorSetLayout());
            else if (type == RenderPathType::RayTracing) m_RenderPath = std::make_unique<RayTracedRenderPath>(m_Context, m_Scene, m_ResourceManager.get(), *m_PipelineManager, m_ResourceManager->GetGlobalDescriptorSetLayout());
            m_RenderPath->Init();
            CH_CORE_INFO("Render Path Switched Successfully.");
        } catch (const std::exception& e) {
            CH_CORE_ERROR("FAILED to switch Render Path: {0}", e.what());
            m_RenderPath = std::make_unique<HybridRenderPath>(m_Context, m_Scene, m_ResourceManager.get(), *m_PipelineManager, m_ResourceManager->GetGlobalDescriptorSetLayout());
            m_RenderPath->Init();
        }
    }

    void Application::ExecuteLoadScene(const std::string& path)
    {
        CH_CORE_INFO("Loading model: {0}", path);
        vkDeviceWaitIdle(m_Context->GetDevice());
        m_Scene = std::make_shared<Scene>(m_Context, m_ResourceManager.get());
        m_Scene->LoadModel(path);
        if (m_RenderPath) m_RenderPath->SetScene(m_Scene);
    }

    void Application::cleanup()
    {
        vkDeviceWaitIdle(m_Context->GetDevice());
        for (auto& layer : m_LayerStack) layer->OnDetach();
        m_LayerStack.clear();
        m_RenderPath.reset();
        m_ImGuiLayer.reset();
        m_Scene.reset();
        m_ResourceManager.reset();
        m_Renderer.reset();
        m_Context.reset();
        m_Window.reset();
        glfwTerminate();
    }

    void Application::PushLayer(const std::shared_ptr<Layer>& layer) { m_LayerStack.push_back(layer); layer->OnAttach(); }
    void Application::SwitchRenderPath(RenderPathType type) { m_PendingRenderPathType = type; m_RenderPathSwitchPending = true; }
    void Application::LoadScene(const std::string& path) { m_PendingScenePath = path; m_SceneLoadPending = true; }
    void Application::Close() { glfwSetWindowShouldClose(m_Window->GetNativeWindow(), GLFW_TRUE); }
    RenderPathType Application::GetCurrentRenderPathType() const { if (m_RenderPath) return m_RenderPath->GetType(); return RenderPathType::Hybrid; }

    bool Application::OnWindowClose(WindowCloseEvent& e) { Close(); return true; }
    bool Application::OnWindowResize(WindowResizeEvent& e) { return false; }
}