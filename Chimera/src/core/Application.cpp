#include "pch.h"
#include "Application.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Backend/Renderer.h"
#include "Renderer/RenderState.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/Backend/PipelineManager.h"
#include "Renderer/Backend/ShaderManager.h"
#include "Renderer/Backend/ShaderRegistry.h"
#include "Renderer/Pipelines/RenderPath.h"
#include "Core/ImGuiLayer.h"
#include "Scene/Scene.h"
#include "Scene/EditorCamera.h"
#include "Core/Input.h"
#include <imgui.h>

namespace Chimera
{
    Application *Application::s_Instance = nullptr;

    Application::Application(const ApplicationSpecification &spec)
        : m_Specification(spec)
    {
        s_Instance = this;
        CH_CORE_INFO("Application: Booting engine...");

        // --- PATH RESOLUTION ---
        std::filesystem::path currentPath = std::filesystem::current_path();
        std::filesystem::path root = currentPath;
        bool foundRoot = false;
        for (int i = 0; i < 5; ++i)
        {
            if (std::filesystem::exists(root / "Chimera") && std::filesystem::exists(root / "scripts"))
            {
                foundRoot = true;
                break;
            }
            if (!root.has_parent_path())
                break;
            root = root.parent_path();
        }

        if (foundRoot)
        {
            if (m_Specification.ShaderSourceDir.empty())
            {
                m_Specification.ShaderSourceDir = (root / "Chimera" / "shaders").string();
                CH_CORE_INFO("Application: Detected Shader Source Dir: {0}", m_Specification.ShaderSourceDir);
            }
        }

        // Initialize Frame Context from Spec
        m_FrameContext.ClearColor = m_Specification.ClearColor;
        m_FrameContext.DisplayMode = m_Specification.DisplayMode;
        m_FrameContext.RenderFlags = m_Specification.RenderFlags;

        m_Window = Window::Create(WindowProps(spec.Name, spec.Width, spec.Height));
        m_Window->SetEventCallback([this](Event &e)
                                   { OnEvent(e); });

        // Trigger lazy initialization of core systems
        m_Context = VulkanContext::Get().GetShared();
        m_ContextAnchor = m_Context;

        m_Renderer.reset(&Renderer::Get());
        m_ResourceManager.reset(&ResourceManager::Get());

        ShaderRegistry::RegisterAll();
        m_PipelineManager = std::make_unique<PipelineManager>();
        m_RenderState = std::make_unique<RenderState>();

        m_ImGuiLayer = std::make_shared<ImGuiLayer>(m_Context);
        PushLayer(m_ImGuiLayer);

        // Load Blue Noise texture for advanced sampling
        auto hBlueNoise = m_ResourceManager->LoadTexture("assets/textures/noise/blue_noise.png", false);
        m_BlueNoiseTextureIndex = hBlueNoise.IsValid() ? (int)hBlueNoise.id : -1;
    }

    Application::~Application()
    {
        CH_CORE_INFO("Application: Destructor started. Starting surgical teardown...");

        std::shared_ptr<VulkanContext> contextKeepAlive = m_Context;

        if (m_Context)
        {
            try
            {
                VkDevice device = m_Context->GetDevice();
                vkDeviceWaitIdle(device);

                // 2. Kill the window callback immediately to stop any incoming events
                if (m_Window)
                {
                    m_Window->SetEventCallback([](Event &) {});
                }

                // 3. Clear Event Queue to release any captures
                {
                    std::scoped_lock<std::mutex> lock(m_EventQueueMutex);
                    m_EventQueue.clear();
                }

                // 4. [STEP 1] DESTROY RENDER PIPELINES
                // Must do this first because RenderGraph/Models need ResourceManager to stay alive during their destruction.
                CH_CORE_INFO("Application: [Step 1/5] Destroying RenderPath and Scene...");
                m_RenderPath.reset();

                // 5. [STEP 2] DESTROY BUSINESS LAYERS (ImGui context must still be alive)
                CH_CORE_INFO("Application: [Step 2/5] Purging LayerStack...");
                while (m_LayerStack.size() > 1)
                {
                    auto layer = m_LayerStack.back();
                    if (layer != m_ImGuiLayer)
                    {
                        CH_CORE_INFO("Application: Releasing layer: {}", layer->GetName());
                        layer->OnDetach();
                        m_LayerStack.pop_back();
                        layer.reset();
                    }
                    else
                    {
                        break;
                    }
                }

                // 6. [STEP 3] DESTROY CORE RENDER SUBSYSTEMS (This releases the 6 leaking buffers)
                CH_CORE_INFO("Application: [Step 3/5] Destroying core rendering subsystems...");
                m_RenderState.reset(); // This destroys per-frame camera/material UBOs

                if (m_PipelineManager)
                {
                    m_PipelineManager->ClearCache();
                    m_PipelineManager.reset();
                }

                ShaderManager::ClearCache();
                m_Renderer.reset(); // This destroys frame-sync objects

                // 7. [STEP 4] CLEAR RESOURCE POOL
                if (m_ResourceManager)
                {
                    CH_CORE_INFO("Application: [Step 4/5] Clearing ResourceManager pool...");
                    CH_CORE_INFO("Application: Calling SetActiveScene(nullptr)...");
                    m_ResourceManager->SetActiveScene(nullptr);
                    CH_CORE_INFO("Application: SetActiveScene(nullptr) completed.");

                    CH_CORE_INFO("Application: Calling m_ResourceManager->Clear()...");
                    m_ResourceManager->Clear();
                    CH_CORE_INFO("Application: m_ResourceManager->Clear() completed.");

                    CH_CORE_INFO("Application: Calling m_ResourceManager.reset()...");
                    m_ResourceManager.reset();
                    CH_CORE_INFO("Application: m_ResourceManager.reset() completed.");
                }

                // 8. [STEP 5] SHUT DOWN IMGUI
                if (m_ImGuiLayer)
                {
                    CH_CORE_INFO("Application: [Step 5/5] Final ImGui shutdown...");
                    m_ImGuiLayer->OnDetach();

                    auto it = std::find(m_LayerStack.begin(), m_LayerStack.end(), m_ImGuiLayer);
                    if (it != m_LayerStack.end())
                        m_LayerStack.erase(it);
                    m_ImGuiLayer.reset();
                }

                CH_CORE_INFO("Application: ImGui shutdown completed.");

                // 9. FINAL HARDWARE FLUSH
                CH_CORE_INFO("Application: Final hardware DeletionQueue flush...");
                m_Context->GetDeletionQueue().FlushAll();

                vkDeviceWaitIdle(device);
                CH_CORE_INFO("Application: Device idle, resetting m_Window...");
                m_Window.reset();
                CH_CORE_INFO("Application: m_Window reset completed.");
            }
            catch (...)
            {
                CH_CORE_ERROR("Application: CRITICAL CRASH during destruction sequence! Potential resource leak.");
            }
        }

        CH_CORE_INFO("Application: Resetting m_ContextAnchor...");
        m_ContextAnchor.reset();
        CH_CORE_INFO("Application: Resetting m_Context...");
        m_Context.reset();
        CH_CORE_INFO("Application: m_Context reset completed.");

        CH_CORE_INFO("Application: Teardown finished successfully. Context count: {}", contextKeepAlive.use_count());
        s_Instance = nullptr;
    }

    void Application::Run()
    {
        while (m_Running)
        {
            {
                std::scoped_lock<std::mutex> lock(m_EventQueueMutex);
                while (!m_EventQueue.empty())
                {
                    auto &func = m_EventQueue.front();
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
                VkCommandBuffer cmd = m_Renderer->BeginFrame();
                
                if (cmd != VK_NULL_HANDLE)
                {
                    uint32_t frameIndex = m_Renderer->GetCurrentFrameIndex();
                    for (auto &layer : m_LayerStack)
                    {
                        layer->OnUpdate(deltaTime);
                    }
                    UpdateGlobalUBO(frameIndex);
                    
                    if (m_RenderPath)
                    {
                        RenderFrameInfo frameInfo{};
                        frameInfo.commandBuffer = cmd;
                        frameInfo.frameIndex = frameIndex;
                        frameInfo.imageIndex = m_Renderer->GetCurrentImageIndex();
                        
                        m_RenderPath->Render(frameInfo);
                    }

                    m_ImGuiLayer->Begin();
                    for (auto &layer : m_LayerStack)
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

    void Application::OnEvent(Event &e)
    {
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent &ev)
                                              { return OnWindowClose(ev); });
        dispatcher.Dispatch<WindowResizeEvent>([this](WindowResizeEvent &ev)
                                               { return OnWindowResize(ev); });

        for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
        {
            if (e.Handled)
            {
                break;
            }
            (*it)->OnEvent(e);
        }
    }

    void Application::UpdateGlobalUBO(uint32_t frameIndex)
    {
        UniformBufferObject ubo{};

        ubo.camera.view = m_FrameContext.View;
        ubo.camera.proj = m_FrameContext.Projection;

        ubo.camera.viewInverse = glm::inverse(ubo.camera.view);
        ubo.camera.projInverse = glm::inverse(ubo.camera.proj);
        ubo.camera.viewProjInverse = glm::inverse(ubo.camera.proj * ubo.camera.view);
        ubo.camera.prevView = m_FrameContext.PrevView;
        ubo.camera.prevProj = m_FrameContext.PrevProj;
        ubo.camera.position = glm::vec4(m_FrameContext.CameraPosition, 1.0f);

        // [TAA] Handle Jitter
        if (m_FrameContext.RenderFlags & RENDER_FLAG_TAA_BIT)
        {
            // [NEW] Check if history is available to prevent first-frame ghosting/jitters
            if (m_RenderPath && m_RenderPath->GetRenderGraph().HasHistory("TAA_Ping"))
            {
                ubo.frameData.w |= RENDER_FLAG_TAA_HISTORY_BIT;
            }
        }

        ubo.camera.jitterData = glm::vec4(m_FrameContext.Jitter.x, m_FrameContext.Jitter.y, m_FrameContext.PrevJitter.x, m_FrameContext.PrevJitter.y);

        if (m_ResourceManager->HasActiveScene())
        {
            auto &sceneLight = m_ResourceManager->GetActiveScene()->GetMainLight();
            ubo.sunLight.direction = sceneLight.direction;
            ubo.sunLight.color = sceneLight.color;
            ubo.sunLight.intensity = glm::vec4(sceneLight.color.a, sceneLight.direction.w, 0.0f, 0.0f);
        }
        else
        {
            ubo.sunLight.direction = glm::vec4(glm::normalize(glm::vec3(-1.0f, -1.0f, -1.0f)), 0.0f);
            ubo.sunLight.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            ubo.sunLight.intensity = glm::vec4(3.0f, 0.05f, 0.0f, 0.0f);
        }

        // Block 1: displayData
        ubo.displayData = glm::vec4(m_FrameContext.ViewportSize.x, m_FrameContext.ViewportSize.y,
                                    1.0f / m_FrameContext.ViewportSize.x, 1.0f / m_FrameContext.ViewportSize.y);

        // Block 2: frameData
        ubo.frameData = glm::uvec4(frameIndex, m_TotalFrameCount, m_FrameContext.DisplayMode, m_FrameContext.RenderFlags);

        // Block 3: postData
        ubo.postData = glm::vec4(m_FrameContext.Exposure, m_FrameContext.AmbientStrength, 0.0f, (float)m_BlueNoiseTextureIndex);

        // Block 4: envData
        int skyboxIdx = (m_ResourceManager->HasActiveScene()) ? (int)m_ResourceManager->GetActiveScene()->GetSkyboxTextureIndex() : -1;
        ubo.envData = glm::vec4((float)skyboxIdx, 0.0f, 0.0f, 0.0f);

        ubo.svgfAlpha = glm::vec4(0.05f, 0.2f, 0.0f, 0.0f); // Default: ColorAlpha=0.05, MomentsAlpha=0.2
        ubo.svgfPhi = glm::vec4(10.0f, 128.0f, 0.1f, 0.0f); // Default: PhiColor=10.0, PhiNormal=128.0, PhiDepth=0.1
        ubo.gpuClearColor = m_FrameContext.ClearColor;

        if (m_ResourceManager->HasActiveScene())
        {
            m_ResourceManager->SyncPrimitivesToGPU(m_ResourceManager->GetActiveScene());
            m_ResourceManager->UpdateSceneDescriptorSet(m_ResourceManager->GetActiveScene(), frameIndex);
        }

        m_ResourceManager->UpdateFrameIndex(frameIndex);
        m_RenderState->Update(frameIndex, ubo);
    }

    bool Application::OnWindowClose(WindowCloseEvent &e)
    {
        m_Running = false;
        return true;
    }

    bool Application::OnWindowResize(WindowResizeEvent &e)
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
            if (m_Renderer)
            {
                m_Renderer->WaitForAllFrames();
                m_Renderer->ResetSwapchainLayouts();
                m_Renderer->ResetFrameState();
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

    uint32_t Application::GetCurrentFrameIndex() const
    {
        return m_Renderer->GetCurrentFrameIndex();
    }
}
