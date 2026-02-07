#include "pch.h"
#include "Application.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Backend/Renderer.h"
#include "Renderer/Backend/PipelineManager.h"
#include "Renderer/Backend/ShaderRegistry.h"
#include "Renderer/Backend/RenderContext.h"
#include "Renderer/RenderState.h"
#include "Renderer/Pipelines/ForwardRenderPath.h"
#include "Renderer/Backend/ShaderManager.h"
#include "Renderer/Pipelines/RenderPathFactory.h"
#include "Renderer/Pipelines/RayTracedRenderPath.h"
#include "Core/Events/ApplicationEvent.h"
#include "Core/Events/KeyEvent.h"
#include "Core/Events/MouseEvent.h"
#include "Assets/AssetImporter.h"
#include "Core/Input.h"
#include "Core/Window.h"
#include "Core/Layer.h"
#include "Core/ImGuiLayer.h"
#include "Utils/VulkanBarrier.h"
#include "Core/EngineConfig.h"
#include "Renderer/Graph/ResourceNames.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include "Renderer/Backend/VulkanCommon.h"
#include "Renderer/Backend/ShaderMetadata.h"
#include <cstdlib>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace Chimera {

    Application* Application::s_Instance = nullptr;

    Application::Application(const ApplicationSpecification& spec)
        : m_Specification(spec)
    {
        s_Instance = this;
        Config::Init();
        ShaderRegistry::Init();
        WindowProps props(spec.Name, spec.Width, spec.Height);
        m_Window = Window::Create(props);
        m_Window->SetEventCallback(BIND_EVENT_FN(Application::OnEvent));

        m_FrameContext.ViewportSize = { (float)spec.Width, (float)spec.Height };
        m_FrameContext.Projection = glm::perspective(glm::radians(45.0f), (float)spec.Width / (float)spec.Height, 0.1f, 1000.0f);
        m_FrameContext.View = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));

        m_Context = std::make_shared<VulkanContext>(m_Window->GetNativeWindow());
        RenderContext::Init(m_Context);

        CH_CORE_INFO("Application: Initializing ResourceManager...");
        m_ResourceManager = std::make_unique<ResourceManager>(m_Context);
        m_ResourceManager->InitGlobalResources();

        m_RenderState = std::make_unique<RenderState>(m_Context);

        ShaderManager::Init(Config::SHADER_DIR, Config::SHADER_SOURCE_DIR);

        CH_CORE_INFO("Application: Initializing PipelineManager...");
        m_PipelineManager = std::make_unique<PipelineManager>(m_Context, *m_ResourceManager);

        CH_CORE_INFO("Application: Initializing Renderer...");
        m_Renderer = std::make_unique<Renderer>(m_Context);

        CH_CORE_INFO("Application: Initializing ImGuiLayer...");
        m_ImGuiLayer = std::make_shared<ImGuiLayer>(m_Context);
        m_ImGuiLayer->OnAttach();

        CH_CORE_INFO("Application: Initializing SceneRenderer...");
        m_SceneRenderer = std::make_unique<SceneRenderer>(m_Context, m_ResourceManager.get(), m_Renderer, m_ImGuiLayer);

        CH_CORE_INFO("Application: Creating Scene...");
        m_Scene = std::make_shared<Scene>(m_Context, m_ResourceManager.get());

        CH_CORE_INFO("Application: Initializing RenderPath...");
        m_RenderPath = RenderPathFactory::Create(m_Context->IsRayTracingSupported() ? RenderPathType::Hybrid : RenderPathType::Forward, m_Context, m_Scene, m_ResourceManager.get(), *m_PipelineManager, m_RenderState->GetLayout());
        
        // FORCED: Inject initial dimensions from window specification
        m_RenderPath->SetViewportSize(m_Specification.Width, m_Specification.Height);
        m_RenderPath->Init();

        CH_CORE_INFO("Application initialized successfully.");
    }

    Application::~Application()
    {
        cleanup();
    }

    void Application::OnEvent(Event& e)
    {
        if (e.GetEventType() == EventType::WindowClose) CH_CORE_INFO("Application: Received WindowCloseEvent");
        
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<WindowCloseEvent>(BIND_EVENT_FN(Application::OnWindowClose));
        dispatcher.Dispatch<WindowResizeEvent>(BIND_EVENT_FN(Application::OnWindowResize));
        
        dispatcher.Dispatch<MouseButtonPressedEvent>([this](MouseButtonEvent& e) {
            ImGui_ImplGlfw_MouseButtonCallback(m_Window->GetNativeWindow(), (int)e.GetMouseButton(), GLFW_PRESS, 0);
            return false;
        });
        dispatcher.Dispatch<MouseButtonReleasedEvent>([this](MouseButtonEvent& e) {
            ImGui_ImplGlfw_MouseButtonCallback(m_Window->GetNativeWindow(), (int)e.GetMouseButton(), GLFW_RELEASE, 0);
            return false;
        });
        dispatcher.Dispatch<MouseMovedEvent>([this](MouseMovedEvent& e) {
            ImGui_ImplGlfw_CursorPosCallback(m_Window->GetNativeWindow(), (double)e.GetX(), (double)e.GetY());
            return false;
        });
        dispatcher.Dispatch<MouseScrolledEvent>([this](MouseScrolledEvent& e) {
            ImGui_ImplGlfw_ScrollCallback(m_Window->GetNativeWindow(), (double)e.GetXOffset(), (double)e.GetYOffset());
            return false;
        });
        dispatcher.Dispatch<KeyPressedEvent>([this](KeyPressedEvent& e) {
            ImGui_ImplGlfw_KeyCallback(m_Window->GetNativeWindow(), (int)e.GetKeyCode(), 0, GLFW_PRESS, 0);
            return false;
        });
        dispatcher.Dispatch<KeyReleasedEvent>([this](KeyReleasedEvent& e) {
            ImGui_ImplGlfw_KeyCallback(m_Window->GetNativeWindow(), (int)e.GetKeyCode(), 0, GLFW_RELEASE, 0);
            return false;
        });
        dispatcher.Dispatch<KeyTypedEvent>([this](KeyTypedEvent& e) {
            ImGui_ImplGlfw_CharCallback(m_Window->GetNativeWindow(), (unsigned int)e.GetKeyCode());
            return false;
        });

        if (m_ImGuiLayer)
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
        try {
            while (!glfwWindowShouldClose(m_Window->GetNativeWindow()))
            {
                float time = (float)glfwGetTime();
                Timestep timestep = time - m_LastFrameTime;
                m_LastFrameTime = time;

                m_Window->OnUpdate();

                {
                    std::scoped_lock<std::mutex> lock(m_EventQueueMutex);
                    while (!m_EventQueue.empty())
                    {
                        auto& func = m_EventQueue.front();
                        func();
                        m_EventQueue.pop();
                    }
                }

                for (auto& layer : m_LayerStack)
                    layer->OnUpdate(timestep);
                
                static float shaderCheckTimer = 0.0f;
                shaderCheckTimer += timestep.GetSeconds();
                if (Config::Settings.EnableHotReload && shaderCheckTimer > 2.0f) // Check every 2 seconds
                {
                    if (ShaderManager::CheckForUpdates())
                    {
                        // Clearing cache is handled internally or via RenderPath reload
                        if (m_RenderPath) m_RenderPath->OnSceneUpdated();
                    }
                    shaderCheckTimer = 0.0f;
                }

                drawFrame();
                
                // Read GPU stats AFTER drawing to ensure they are ready for next frame's UI
                if (m_RenderPath && m_RenderPath->GetRenderGraphPtr()) {
                    m_RenderPath->GetRenderGraphPtr()->GatherPerformanceStatistics();
                }
            }
        } catch (const std::exception& e) {
            CH_CORE_FATAL("FATAL ERROR in main loop: {0}", e.what());
        } catch (...) {
            CH_CORE_FATAL("UNKNOWN FATAL ERROR in main loop");
        }
        CH_CORE_INFO("Main loop ended.");
    }

    void Application::RecompileShaders()
    {
        CH_CORE_INFO("Recompiling shaders...");
        int res = std::system("powershell.exe -ExecutionPolicy Bypass -File ../../../scripts/CompileShaders.ps1");
        if (res == 0) RequestShaderReload();
        else CH_CORE_ERROR("Failed to recompile shaders.");
    }

    void Application::RequestShaderReload()
    {
        QueueEvent([this]() {
            vkDeviceWaitIdle(m_Context->GetDevice());
            m_PipelineManager->ClearCache();
            if (m_RenderPath) m_RenderPath->OnSceneUpdated();
            CH_CORE_INFO("Manual shader reload executed via Event Queue.");
        });
    }

    void Application::drawFrame()
    {
        if (!m_SceneRenderer) return;
        if (!m_Scene) return;
        if (!m_RenderPath) return;

        m_TotalFrameCount++;
        m_SceneRenderer->Render(m_Scene.get(), m_RenderPath.get(), m_FrameContext, m_LayerStack);
    }

    void Application::ExecuteRenderPathSwitch(RenderPathType type)
    {
        CH_CORE_INFO("Switching Render Path to: {0}", (int)type);
        vkDeviceWaitIdle(m_Context->GetDevice());
        m_RenderPath.reset(); 
        
        m_RenderPath = RenderPathFactory::Create(type, m_Context, m_Scene, m_ResourceManager.get(), *m_PipelineManager, m_RenderState->GetLayout());
        
        if (m_RenderPath) {
            m_RenderPath->Init();
            CH_CORE_INFO("Render Path Switched Successfully.");
        }
    }

    void Application::ExecuteLoadScene(const std::string& path)
    {
        CH_CORE_INFO("Loading model: {0}", path);
        vkDeviceWaitIdle(m_Context->GetDevice());
        
        // 1. If we are replacing the whole scene, we must ensure the graph is NOT referencing it
        // Note: LoadModel currently ADDS to the scene, but if we want to be safe:
        if (m_RenderPath) m_RenderPath->OnSceneUpdated(); 

        if (!m_Scene)
            m_Scene = std::make_shared<Scene>(m_Context, m_ResourceManager.get());
            
        m_Scene->LoadModel(path);

        // 2. Rebuild graph now that scene is updated
        if (m_RenderPath) {
            m_RenderPath->SetScene(m_Scene);
            m_RenderPath->Update(); 
        }
    }

    void Application::LoadSkybox(const std::string& path)
    {
        QueueEvent([this, path]() { ExecuteLoadSkybox(path); });
    }

    void Application::ExecuteLoadSkybox(const std::string& path)
    {
        CH_CORE_INFO("Loading skybox: {0}", path);
        vkDeviceWaitIdle(m_Context->GetDevice());
        if (m_Scene) {
            m_Scene->LoadSkybox(path);
            if (m_RenderPath) {
                m_RenderPath->OnSceneUpdated();
                m_RenderPath->Update();
            }
        }
    }

    void Application::ExecuteClearScene()
    {
        CH_CORE_INFO("Clearing scene.");
        vkDeviceWaitIdle(m_Context->GetDevice());

        // 1. Explicitly clear the RenderGraph first to release descriptors pointing to the old scene
        if (m_RenderPath) {
            // We need a way to clear the graph without immediately rebuilding it with the OLD scene.
            // For now, let's just swap the scene and then rebuild.
        }

        m_Scene = std::make_shared<Scene>(m_Context, m_ResourceManager.get());

        // 2. Rebuild graph AFTER scene is cleared and swapped
        if (m_RenderPath) {
            m_RenderPath->SetScene(m_Scene);
            m_RenderPath->Update();
        }
    }

    void Application::cleanup()
    {
        vkDeviceWaitIdle(m_Context->GetDevice());
        for (auto& layer : m_LayerStack) layer->OnDetach();
        m_LayerStack.clear();
        
        m_RenderPath.reset();
        m_SceneRenderer.reset();
        m_PipelineManager.reset();
        
        if (m_ImGuiLayer) {
            m_ImGuiLayer->OnDetach();
            m_ImGuiLayer.reset();
        }

        m_Scene.reset();
        m_ResourceManager.reset();
        m_RenderState.reset();
        m_Renderer.reset();
        RenderContext::Shutdown();
        m_Context.reset();
        m_Window.reset();
        glfwTerminate();
    }

    void Application::PushLayer(const std::shared_ptr<Layer>& layer) { m_LayerStack.push_back(layer); layer->OnAttach(); }
    void Application::SwitchRenderPath(RenderPathType type) { QueueEvent([this, type]() { ExecuteRenderPathSwitch(type); }); }
    void Application::LoadScene(const std::string& path) { QueueEvent([this, path]() { ExecuteLoadScene(path); }); }
    void Application::ClearScene() { QueueEvent([this]() { ExecuteClearScene(); }); }
    void Application::Close() { glfwSetWindowShouldClose(m_Window->GetNativeWindow(), GLFW_TRUE); }

    VkCommandBuffer Application::GetCommandBuffer(bool begin) { return RenderContext::BeginSingleTimeCommands(); }
    void Application::FlushCommandBuffer(VkCommandBuffer commandBuffer) { RenderContext::EndSingleTimeCommands(commandBuffer); }

    RenderPathType Application::GetCurrentRenderPathType() const
    {
        if (dynamic_cast<ForwardRenderPath*>(m_RenderPath.get())) return RenderPathType::Forward;
        if (dynamic_cast<RayTracedRenderPath*>(m_RenderPath.get())) return RenderPathType::RayTracing;
        return RenderPathType::Hybrid;
    }

    bool Application::OnWindowClose(WindowCloseEvent& e) { Close(); return true; }
    bool Application::OnWindowResize(WindowResizeEvent& e) { if (e.GetWidth() == 0 || e.GetHeight() == 0) return false; m_Context->RecreateSwapChain(); return false; }

}
