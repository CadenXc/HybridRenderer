#include "pch.h"
#include "Application.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/Backend/PipelineManager.h"
#include "Renderer/Backend/RenderContext.h"
#include "Renderer/RenderState.h"
#include "Renderer/Backend/Renderer.h"
#include "Renderer/Pipelines/RenderPathFactory.h"
#include "Core/ImGuiLayer.h"
#include "Scene/Scene.h"
#include "Core/Input.h"
#include "Core/EngineConfig.h"
#include <GLFW/glfw3.h>

namespace Chimera
{
    Application* Application::s_Instance = nullptr;

    Application::Application(const ApplicationSpecification& spec)
        : m_Specification(spec)
    {
        s_Instance = this;
        Config::Init();

        CH_CORE_INFO("Application: Booting engine...");

        // 1. Foundation
        m_Window = Window::Create(WindowProps(spec.Name, spec.Width, spec.Height));
        m_Window->SetEventCallback(BIND_EVENT_FN(Application::OnEvent));

        // 2. Vulkan Core & Singletons
        m_Context = std::make_shared<VulkanContext>(m_Window->GetNativeWindow());
        
        m_ResourceManager = std::make_unique<ResourceManager>();
        m_ResourceManager->InitGlobalResources();
        
        m_PipelineManager = std::make_unique<PipelineManager>();
        
        m_RenderState = std::make_unique<RenderState>();
        
        // 3. Rendering Frontend
        m_Renderer = std::make_unique<Renderer>();
        
        // 4. UI & High-level Systems
        m_ImGuiLayer = std::make_shared<ImGuiLayer>(m_Context);

        PushOverlay(m_ImGuiLayer);
    }

    Application::~Application()
    {
        CH_CORE_INFO("Application: Shutting down...");
        
        if (m_Context) {
            vkDeviceWaitIdle(m_Context->GetDevice());
        }

        // 1. NOTIFY AND CLEAR LAYERS - Must call OnDetach explicitly!
        for (auto& layer : m_LayerStack) {
            CH_CORE_TRACE("Application: Detaching layer '{0}'", layer->GetName());
            layer->OnDetach();
        }
        
        m_ImGuiLayer.reset();
        m_LayerStack.clear(); 
        
        CH_CORE_INFO("Application: Layers cleared.");

        // 3. Destroy Renderer (Critical for CommandBuffers/Semaphores)
        m_Renderer.reset();

        // 4. Destroy Managers
        m_PipelineManager.reset();
        m_ResourceManager.reset();
        m_RenderState.reset();

        // 5. Finally destroy Context (VkDevice)
        m_Context.reset();
        
        CH_CORE_INFO("Application: Shutdown complete.");
    }

    void Application::Run()
    {
        while (m_Running)
        {
            float time = (float)glfwGetTime();
            Timestep timestep = time - m_LastFrameTime;
            m_LastFrameTime = time;

            if (!m_Minimized)
            {
                ProcessEventQueue();
                DrawFrame(timestep);
            }

            m_Window->OnUpdate();
        }
    }

    void Application::DrawFrame(Timestep ts)
    {
        uint32_t frameIndex = m_TotalFrameCount % MAX_FRAMES_IN_FLIGHT;

        // 1. Begin Frame FIRST - This waits for the GPU fence
        VkCommandBuffer cmd = m_Renderer->BeginFrame();
        if (cmd == VK_NULL_HANDLE)
        {
            return;
        }

        // 2. Update Global State AFTER waiting for GPU
        UpdateGlobalUBO(frameIndex);

        // 3. Update Layers
        for (auto& layer : m_LayerStack)
        {
            layer->OnUpdate(ts);
        }

        // 4. Render UI
        m_ImGuiLayer->Begin();
        for (auto& layer : m_LayerStack)
        {
            layer->OnImGuiRender();
        }
        m_ImGuiLayer->End(cmd);

        // 5. Submit
        m_Renderer->EndFrame();
        
        m_TotalFrameCount++;
    }

    void Application::UpdateGlobalUBO(uint32_t frameIndex)
    {
        UniformBufferObject ubo{};
        
        // Safety: Ensure we have a valid camera view
        if (glm::length(m_FrameContext.View[0]) < 0.001f) {
            m_FrameContext.View = glm::mat4(1.0f);
            m_FrameContext.Projection = glm::perspective(glm::radians(45.0f), 1.77f, 0.1f, 1000.0f);
        }

        ubo.view = m_FrameContext.View;
        ubo.proj = m_FrameContext.Projection;
        ubo.viewInverse = glm::inverse(ubo.view);
        ubo.projInverse = glm::inverse(ubo.proj);
        ubo.viewProjInverse = ubo.viewInverse * ubo.projInverse;
        ubo.cameraPos = glm::vec4(m_FrameContext.CameraPosition, 1.0f);
        
        ubo.prevView = m_PrevView;
        ubo.prevProj = m_PrevProj;
        
        ubo.displaySize = { (float)m_Specification.Width, (float)m_Specification.Height };
        ubo.displaySizeInverse = { 1.0f / ubo.displaySize.x, 1.0f / ubo.displaySize.y };
        ubo.frameIndex = frameIndex;
        ubo.frameCount = m_TotalFrameCount;
        
        ResourceManager::Get().UpdateFrameIndex(frameIndex);
        
        if (m_ActiveScene)
        {
            ResourceManager::Get().UpdateSceneDescriptorSet(m_ActiveScene, frameIndex);
            ubo.directionalLight = m_ActiveScene->GetLight();
        }

        m_RenderState->Update(frameIndex, ubo);

        m_PrevView = ubo.view;
        m_PrevProj = ubo.proj;
    }

    void Application::OnEvent(Event& e)
    {
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<WindowCloseEvent>(BIND_EVENT_FN(Application::OnWindowClose));
        dispatcher.Dispatch<WindowResizeEvent>(BIND_EVENT_FN(Application::OnWindowResize));

        for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
        {
            if (e.Handled)
            {
                break;
            }
            (*it)->OnEvent(e);
        }
    }

    bool Application::OnWindowResize(WindowResizeEvent& e)
    {
        if (e.GetWidth() == 0 || e.GetHeight() == 0)
        {
            m_Minimized = true;
            return false;
        }

        m_Minimized = false;
        m_Specification.Width = e.GetWidth();
        m_Specification.Height = e.GetHeight();

        QueueEvent([this, w = e.GetWidth(), h = e.GetHeight()]()
        {
            vkDeviceWaitIdle(m_Context->GetDevice());
            m_Context->RecreateSwapChain();
        });

        return false;
    }

    void Application::ProcessEventQueue()
    {
        std::lock_guard<std::mutex> lock(m_EventQueueMutex);
        if (!m_EventQueue.empty()) {
            CH_CORE_TRACE("Application: Processing {0} events in queue...", m_EventQueue.size());
        }
        
        while (!m_EventQueue.empty())
        {
            auto func = std::move(m_EventQueue.front());
            m_EventQueue.pop_front();
            
            CH_CORE_TRACE("Application: Executing event task...");
            func();
            CH_CORE_TRACE("Application: Event task finished.");
        }
    }

    void Application::Close() 
    { 
        m_Running = false; 
    }

    void Application::PushLayer(std::shared_ptr<Layer> layer) 
    { 
        m_LayerStack.emplace(m_LayerStack.begin() + m_LayerIndex, layer); 
        m_LayerIndex++; 
        layer->OnAttach(); 
    }

    void Application::PushOverlay(std::shared_ptr<Layer> layer) 
    { 
        m_LayerStack.emplace_back(layer); 
        layer->OnAttach(); 
    }

    VkCommandBuffer Application::GetCommandBuffer(bool begin) { return m_Renderer->BeginFrame(); }
    void Application::FlushCommandBuffer(VkCommandBuffer cmd) { m_Renderer->EndFrame(); }
    uint32_t Application::GetCurrentImageIndex() const { return m_Renderer->GetCurrentImageIndex(); }
    bool Application::OnWindowClose(WindowCloseEvent& e) { m_Running = false; return true; }
}
