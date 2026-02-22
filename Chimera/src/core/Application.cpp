#include "pch.h"
#include "Application.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Backend/Renderer.h"
#include "Renderer/RenderState.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/Backend/PipelineManager.h"
#include "Renderer/Backend/ShaderManager.h"
#include "Renderer/Pipelines/RenderPath.h"
#include "Core/ImGuiLayer.h"
#include "Scene/Scene.h"
#include "Scene/EditorCamera.h"
#include "Core/Input.h"
#include <imgui.h>

namespace Chimera
{
    Application* Application::s_Instance = nullptr;

    Application::Application(const ApplicationSpecification& spec)
        : m_Specification(spec)
    {
        s_Instance = this;

        CH_CORE_INFO("Application: Booting engine...");

        m_Window = Window::Create(WindowProps(spec.Name, spec.Width, spec.Height));
        m_Window->SetEventCallback([this](Event& e)
        {
            OnEvent(e);
        });

        m_Context = std::make_shared<VulkanContext>(m_Window->GetNativeWindow());
        m_Renderer = std::make_unique<Renderer>();
        m_ResourceManager = std::make_unique<ResourceManager>();
        m_ResourceManager->InitGlobalResources();
        
        m_PipelineManager = std::make_unique<PipelineManager>();
        m_RenderState = std::make_unique<RenderState>();
        
        m_ImGuiLayer = std::make_shared<ImGuiLayer>(m_Context);
        PushLayer(m_ImGuiLayer);
    }

    Application::~Application()
    {
        CH_CORE_INFO("Application: Shutting down...");
        
        if (m_Context)
        {
            VkDevice device = m_Context->GetDevice();
            vkDeviceWaitIdle(device);

            // 1. Clear layers first (they might hold references to textures/buffers)
            m_LayerStack.clear();
            m_ImGuiLayer.reset();

            // 2. Clear RenderPath and Scene resources
            m_RenderPath.reset();
            
            // 3. Clear global managers that hold GPU resources
            m_RenderState.reset();
            m_PipelineManager.reset();
            
            // 4. ResourceManager must be cleared while VulkanContext is still alive
            if (m_ResourceManager)
            {
                m_ResourceManager->ClearResourceFreeQueue(0);
                m_ResourceManager->ClearResourceFreeQueue(1);
                m_ResourceManager->ClearResourceFreeQueue(2);
                m_ResourceManager.reset();
            }

            // 5. Finally reset Renderer and Context
            m_Renderer.reset();

            // Explicitly flush the context deletion queue one last time before it's gone
            m_Context->GetDeletionQueue().FlushAll();
            m_Context.reset();
        }
    }

    void Application::Run()
    {
        while (m_Running)
        {
            // Process queued events
            {
                std::scoped_lock<std::mutex> lock(m_EventQueueMutex);
                while (!m_EventQueue.empty())
                {
                    auto& func = m_EventQueue.front();
                    if (func)
                    {
                        func();
                    }
                    m_EventQueue.pop_front();
                }
            }

            float time = (float)glfwGetTime();
            float deltaTime = time - m_LastFrameTime;
            m_LastFrameTime = time;

            if (!m_Minimized)
            {
                // 2. Rendering
                VkCommandBuffer cmd = m_Renderer->BeginFrame();
                if (cmd != VK_NULL_HANDLE)
                {
                    uint32_t frameIndex = m_Renderer->GetCurrentFrameIndex();

                    // 1. Logic Update (Now within BeginFrame/EndFrame so IsFrameInProgress is true)
                    for (auto& layer : m_LayerStack)
                    {
                        layer->OnUpdate(deltaTime);
                    }
                    
                    // Sync UBO
                    UpdateGlobalUBO(frameIndex);

                    // UI Overlay
                    m_ImGuiLayer->Begin();
                    for (auto& layer : m_LayerStack)
                    {
                        layer->OnImGuiRender();
                    }
                    m_ImGuiLayer->End(cmd);

                    m_Renderer->EndFrame();
                    m_TotalFrameCount++;
                }
            }

            m_Window->OnUpdate();
        }
    }

    void Application::OnEvent(Event& e)
    {
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent& ev)
        {
            return OnWindowClose(ev);
        });
        dispatcher.Dispatch<WindowResizeEvent>([this](WindowResizeEvent& ev)
        {
            return OnWindowResize(ev);
        });

        for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
        {
            if (e.Handled) break;
            (*it)->OnEvent(e);
        }
    }

    void Application::UpdateGlobalUBO(uint32_t frameIndex)
    {
        UniformBufferObject ubo{};
        
        // --- Camera Data ---
        ubo.camera.view = m_FrameContext.View;
        ubo.camera.proj = m_FrameContext.Projection;
        ubo.camera.viewInverse = glm::inverse(ubo.camera.view);
        ubo.camera.projInverse = glm::inverse(ubo.camera.proj);
        ubo.camera.viewProjInverse = glm::inverse(ubo.camera.proj * ubo.camera.view);
        ubo.camera.prevView = m_PrevView;
        ubo.camera.prevProj = m_PrevProj;
        ubo.camera.position = glm::vec4(m_FrameContext.CameraPosition, 1.0f);
        
        // --- Light Data ---
        if (m_ResourceManager->HasActiveScene())
        {
            auto& sceneLight = m_ResourceManager->GetActiveScene()->GetLight();
            ubo.sunLight.direction = sceneLight.direction;
            ubo.sunLight.color = sceneLight.color;
            ubo.sunLight.intensity = sceneLight.intensity;
            // Radius is already stored in sceneLight.intensity.y by EditorLayer
        }
        else
        {
            ubo.sunLight.direction = glm::vec4(glm::normalize(glm::vec3(-1.0f, -1.0f, -1.0f)), 0.0f);
            ubo.sunLight.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            ubo.sunLight.intensity = glm::vec4(3.0f, m_FrameContext.LightRadius, 0.0f, 0.0f);
        }

        // --- SVGF Parameters ---
        ubo.svgf.alphaColor = m_FrameContext.SVGFAlphaColor;
        ubo.svgf.alphaMoments = m_FrameContext.SVGFAlphaMoments;
        ubo.svgf.phiColor = m_FrameContext.SVGFPhiColor;
        ubo.svgf.phiNormal = m_FrameContext.SVGFPhiNormal;
        ubo.svgf.phiDepth = m_FrameContext.SVGFPhiDepth;

        // --- Scene & Rendering Control ---
        ubo.displaySize = m_FrameContext.ViewportSize;
        ubo.displaySizeInverse = 1.0f / ubo.displaySize;
        ubo.frameIndex = frameIndex;
        ubo.frameCount = m_TotalFrameCount;
        
        ubo.displayMode = m_FrameContext.DisplayMode;
        ubo.renderFlags = m_FrameContext.RenderFlags;
        ubo.exposure = m_FrameContext.Exposure;
        ubo.ambientStrength = m_FrameContext.AmbientStrength;
        ubo.bloomStrength = m_FrameContext.BloomStrength;

        m_ResourceManager->UpdateFrameIndex(frameIndex);
        m_RenderState->Update(frameIndex, ubo);

        m_PrevView = ubo.camera.view;
        m_PrevProj = ubo.camera.proj;
    }

    bool Application::OnWindowClose(WindowCloseEvent& e)
    {
        m_Running = false;
        return true;
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

        m_Renderer->OnResize(e.GetWidth(), e.GetHeight());
        return false;
    }

    void Application::PushLayer(std::shared_ptr<Layer> layer)
    {
        m_LayerStack.emplace_back(layer);
        layer->OnAttach();
    }

    void Application::SwitchRenderPath(std::unique_ptr<RenderPath> path)
    {
        if (m_Context)
        {
            vkDeviceWaitIdle(m_Context->GetDevice());
            
            // [STABILITY] Forcefully clear any lingering image states from the old path
            if (m_Renderer)
            {
                m_Renderer->ResetSwapchainLayouts();
            }
        }
        m_RenderPath = std::move(path);
        if (m_RenderPath)
        {
            m_RenderPath->SetViewportSize(m_Specification.Width, m_Specification.Height);
        }
    }

    void Application::Close()
    {
        m_Running = false;
    }

    uint32_t Application::GetCurrentImageIndex() const
    {
        return m_Renderer->GetCurrentImageIndex();
    }
}
