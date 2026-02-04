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

        m_SceneRenderer = std::make_unique<SceneRenderer>(m_Context, m_ResourceManager.get(), m_Renderer, m_ImGuiLayer.get());

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

            // Reference Walnut: Process custom event queue
            {
                std::scoped_lock<std::mutex> lock(m_EventQueueMutex);
                while (!m_EventQueue.empty())
                {
                    auto& func = m_EventQueue.front();
                    func();
                    m_EventQueue.pop();
                }
            }

            // Camera is now updated via Layer::OnUpdate (e.g. EditorLayer)
            for (auto& layer : m_LayerStack)
                layer->OnUpdate(timestep);
            
            // Hot reload logic using Config settings
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
        if (res == 0) RequestShaderReload();
        else CH_CORE_ERROR("Failed to recompile shaders.");
    }

    void Application::RequestShaderReload()
    {
        QueueEvent([this]() {
            vkDeviceWaitIdle(m_Context->GetDevice());
            m_PipelineManager->ClearCache();
            m_RenderPath->OnSceneUpdated();
            CH_CORE_INFO("Manual shader reload executed via Event Queue.");
        });
    }

    void Application::drawFrame()
    {
        m_SceneRenderer->Render(m_Scene.get(), m_RenderPath.get(), m_FrameContext, m_LayerStack);
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
        m_SceneRenderer.reset();
        m_ImGuiLayer.reset();
        m_Scene.reset();
        m_ResourceManager.reset();
        m_Renderer.reset();
        m_Context.reset();
        m_Window.reset();
        glfwTerminate();
    }

    void Application::PushLayer(const std::shared_ptr<Layer>& layer) { m_LayerStack.push_back(layer); layer->OnAttach(); }
    
    void Application::SwitchRenderPath(RenderPathType type) 
    { 
        QueueEvent([this, type]() { ExecuteRenderPathSwitch(type); });
    }

    void Application::LoadScene(const std::string& path) 
    { 
        QueueEvent([this, path]() { ExecuteLoadScene(path); });
    }

    VkCommandBuffer Application::GetCommandBuffer(bool begin)
    {
        return s_Instance->m_Context->BeginSingleTimeCommands();
    }

    void Application::FlushCommandBuffer(VkCommandBuffer commandBuffer)
    {
        s_Instance->m_Context->EndSingleTimeCommands(commandBuffer);
    }

    void Application::Close() { glfwSetWindowShouldClose(m_Window->GetNativeWindow(), GLFW_TRUE); }
    RenderPathType Application::GetCurrentRenderPathType() const { if (m_RenderPath) return m_RenderPath->GetType(); return RenderPathType::Hybrid; }

    bool Application::OnWindowClose(WindowCloseEvent& e) { Close(); return true; }
    bool Application::OnWindowResize(WindowResizeEvent& e) { return false; }
}