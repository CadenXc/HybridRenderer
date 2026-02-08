#include "pch.h"
#include "Application.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/Backend/PipelineManager.h"
#include "Renderer/Backend/Renderer.h"
#include "Renderer/Backend/RenderContext.h"
#include "Renderer/Backend/ShaderManager.h"
#include "Renderer/Pipelines/RenderPath.h"
#include "Renderer/Pipelines/ForwardRenderPath.h"
#include "Renderer/Pipelines/HybridRenderPath.h"
#include "Renderer/Pipelines/RayTracedRenderPath.h"
#include "Renderer/SceneRenderer.h"
#include "Renderer/RenderState.h"
#include "Scene/Scene.h"
#include "Core/ImGuiLayer.h"
#include "Core/EngineConfig.h"
#include "Core/Events/ApplicationEvent.h"
#include "Core/Input.h"
#include <GLFW/glfw3.h>

namespace Chimera {

    Application* Application::s_Instance = nullptr;

    Application::Application(const ApplicationSpecification& spec)
        : m_Specification(spec)
    {
        s_Instance = this;

        // 0. Initialize Config and ShaderManager
        Config::Init();
        ShaderManager::Init(Config::SHADER_DIR, Config::SHADER_SOURCE_DIR);

        // 1. Create Window
        WindowProps props(spec.Name, spec.Width, spec.Height);
        m_Window = Window::Create(props);
        m_Window->SetEventCallback(BIND_EVENT_FN(Application::OnEvent));

        // 2. Vulkan Context
        m_Context = std::make_shared<VulkanContext>(m_Window->GetNativeWindow());
        RenderContext::Init(m_Context);

        // 3. Resource Manager
        m_ResourceManager = std::make_unique<ResourceManager>(m_Context);
        m_ResourceManager->InitGlobalResources();

        // 4. Pipeline Manager
        m_PipelineManager = std::make_unique<PipelineManager>(m_Context, *m_ResourceManager);

        // 5. Render State
        m_RenderState = std::make_unique<RenderState>(m_Context);

        // 6. Renderer
        m_Renderer = std::make_shared<Renderer>(m_Context);

        // 7. ImGui
        m_ImGuiLayer = std::make_shared<ImGuiLayer>(m_Context);
        PushOverlay(m_ImGuiLayer);

        // 8. Scene
        m_Scene = std::make_shared<Scene>(m_Context, m_ResourceManager.get());
        
        // 9. Scene Renderer
        m_SceneRenderer = std::make_unique<SceneRenderer>(m_Context, m_ResourceManager.get(), m_Renderer, m_ImGuiLayer);

        // 10. Render Path (Default Forward)
        SwitchRenderPath(RenderPathType::Forward);
    }

    Application::~Application() {
        if (m_Context && m_Context->GetDevice()) {
            vkDeviceWaitIdle(m_Context->GetDevice());
        }

        m_RenderPath.reset();
        m_SceneRenderer.reset();
        m_Scene.reset();
        m_ImGuiLayer.reset();
        m_LayerStack.clear();
        m_Renderer.reset();
        m_RenderState.reset();
        m_PipelineManager.reset();
        m_ResourceManager.reset();
        m_Context.reset();
        m_Window.reset();
    }

    void Application::Run() {
        while (m_Running) {
            float time = (float)glfwGetTime();
            float timestep = time - m_LastFrameTime;
            m_LastFrameTime = time;

            if (!m_Minimized) {
                // Process Event Queue
                {
                    std::lock_guard<std::mutex> lock(m_EventQueueMutex);
                    while (!m_EventQueue.empty()) {
                        m_EventQueue.front()();
                        m_EventQueue.pop_front();
                    }
                }

                VkCommandBuffer cmd = m_Renderer->BeginFrame();
                if (cmd == VK_NULL_HANDLE) {
                    m_Window->OnUpdate();
                    continue;
                }
                
                uint32_t frameIndex = m_Renderer->GetCurrentFrameIndex();
                uint32_t imageIndex = m_Renderer->GetCurrentImageIndex();

                // Update Layers (this updates the camera)
                for (std::shared_ptr<Layer> layer : m_LayerStack)
                    layer->OnUpdate(Timestep(timestep));

                // Update RenderState with CURRENT frame data
                GlobalFrameData globalData{};
                globalData.view = m_FrameContext.View;
                globalData.proj = m_FrameContext.Projection;
                globalData.viewInverse = glm::inverse(m_FrameContext.View);
                globalData.projInverse = glm::inverse(m_FrameContext.Projection);
                globalData.viewProjInverse = glm::inverse(m_FrameContext.Projection * m_FrameContext.View);
                globalData.prevView = globalData.view; 
                globalData.prevProj = globalData.proj;
                
                if (m_Scene) {
                    globalData.directionalLight = m_Scene->GetLight();
                }

                globalData.displaySize = m_FrameContext.ViewportSize;
                globalData.displaySizeInverse = 1.0f / m_FrameContext.ViewportSize;
                globalData.frameIndex = m_TotalFrameCount;
                globalData.frameCount = m_TotalFrameCount;
                globalData.displayMode = 0;
                globalData.cameraPos = glm::vec4(m_FrameContext.CameraPosition, 1.0f);
                
                m_RenderState->Update(frameIndex, globalData);

                // Render Scene
                if (m_RenderPath) {
                    RenderFrameInfo frameInfo{};
                    frameInfo.commandBuffer = cmd;
                    frameInfo.frameIndex = frameIndex;
                    frameInfo.imageIndex = imageIndex;
                    frameInfo.globalSet = m_RenderState->GetDescriptorSet(frameIndex);

                    m_RenderPath->Render(frameInfo);
                }

                // Render UI (As an Overlay on top of the RenderPath output)
                m_ImGuiLayer->Begin();
                for (std::shared_ptr<Layer> layer : m_LayerStack)
                    layer->OnUIRender();
                m_ImGuiLayer->End(cmd);

                m_Renderer->EndFrame();
                m_TotalFrameCount++;
            }

            m_Window->OnUpdate();
        }
    }

    void Application::drawFrame() {} // Legacy stub

    void Application::OnEvent(Event& e) {
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<WindowCloseEvent>(BIND_EVENT_FN(Application::OnWindowClose));
        dispatcher.Dispatch<WindowResizeEvent>(BIND_EVENT_FN(Application::OnWindowResize));

        for (auto it = m_LayerStack.end(); it != m_LayerStack.begin(); ) {
            (*--it)->OnEvent(e);
            if (e.Handled) break;
        }
    }

    bool Application::OnWindowClose(WindowCloseEvent& e) {
        m_Running = false;
        return true;
    }

    bool Application::OnWindowResize(WindowResizeEvent& e) {
        if (e.GetWidth() == 0 || e.GetHeight() == 0) {
            m_Minimized = true;
            return false;
        }
        m_Minimized = false;
        m_Renderer->OnResize(e.GetWidth(), e.GetHeight());
        if (m_RenderPath) m_RenderPath->SetViewportSize(e.GetWidth(), e.GetHeight());
        return false;
    }

    void Application::PushLayer(std::shared_ptr<Layer> layer) {
        m_LayerStack.emplace(m_LayerStack.begin() + m_LayerIndex, layer);
        m_LayerIndex++;
        layer->OnAttach();
    }

    void Application::PushOverlay(std::shared_ptr<Layer> layer) {
        m_LayerStack.emplace_back(layer);
        layer->OnAttach();
    }

    void Application::SwitchRenderPath(RenderPathType type) {
        QueueEvent([this, type]() { ExecuteRenderPathSwitch(type); });
    }

    void Application::ExecuteRenderPathSwitch(RenderPathType type) {
        vkDeviceWaitIdle(m_Context->GetDevice());
        m_RenderPath.reset();
        
        switch (type) {
            case RenderPathType::Forward:
                m_RenderPath = std::make_unique<ForwardRenderPath>(m_Context, m_Scene, m_ResourceManager.get(), *m_PipelineManager);
                break;
            case RenderPathType::Hybrid:
                m_RenderPath = std::make_unique<HybridRenderPath>(m_Context, m_Scene, m_ResourceManager.get(), *m_PipelineManager);
                break;
            case RenderPathType::RayTracing:
                m_RenderPath = std::make_unique<RayTracedRenderPath>(m_Context, m_Scene, m_ResourceManager.get(), *m_PipelineManager);
                break;
        }
        if (m_RenderPath) m_RenderPath->Init();
    }

    void Application::RecompileShaders() {
         // TODO: Implement shader reloading
    }
    
    void Application::RequestShaderReload() { RecompileShaders(); }

    void Application::LoadScene(const std::string& path) {
        QueueEvent([this, path]() { ExecuteLoadScene(path); });
    }

    void Application::ExecuteLoadScene(const std::string& path) {
        CH_CORE_INFO("Application: Loading Scene: {0}", path);
        m_Scene->LoadModel(path); 
    }

    void Application::ClearScene() {
        QueueEvent([this]() { ExecuteClearScene(); });
    }

    void Application::ExecuteClearScene() {
        // m_Scene->Clear();
    }

    void Application::LoadSkybox(const std::string& path) {
        QueueEvent([this, path]() { ExecuteLoadSkybox(path); });
    }

    void Application::ExecuteLoadSkybox(const std::string& path) {
        m_ResourceManager->LoadHDRTexture(path);
        // Set as skybox...
    }

    void Application::Close() { m_Running = false; }

    VkCommandBuffer Application::GetCommandBuffer(bool begin) {
        return m_Renderer->GetActiveCommandBuffer(); // Simplified
    }

    void Application::FlushCommandBuffer(VkCommandBuffer cmd) {
        // Submit immediately
    }

    uint32_t Application::GetCurrentImageIndex() const {
        return m_Renderer ? m_Renderer->GetCurrentImageIndex() : 0;
    }

    RenderPathType Application::GetCurrentRenderPathType() const {
        if (!m_RenderPath) return RenderPathType::Forward;
        return m_RenderPath->GetType();
    }

}