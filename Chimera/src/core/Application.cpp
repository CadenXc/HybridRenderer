#include "pch.h"
#include "Application.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Backend/Renderer.h"
#include "Renderer/Backend/PipelineManager.h"
#include "Renderer/Pipelines/ForwardRenderPath.h"
#include "Renderer/Pipelines/HybridRenderPath.h"
#include "Renderer/Pipelines/RayTracedRenderPath.h"
#include "Core/Events/ApplicationEvent.h"
#include "Core/Events/KeyEvent.h"
#include "Core/Events/MouseEvent.h"
#include "Assets/AssetImporter.h"
#include "Core/Input.h"
#include "Core/Window.h"
#include "Core/Layer.h"
#include "Utils/VulkanBarrier.h"
#include "Core/EngineConfig.h"
#include "Renderer/Graph/ResourceNames.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include "Renderer/Backend/VulkanCommon.h"
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

        CH_CORE_INFO("Application: Initializing ResourceManager...");
        m_ResourceManager = std::make_unique<ResourceManager>(m_Context);
        m_ResourceManager->InitGlobalResources();

        CH_CORE_INFO("Application: Initializing PipelineManager...");
        m_PipelineManager = std::make_unique<PipelineManager>(m_Context, *m_ResourceManager);

        CH_CORE_INFO("Application: Initializing Renderer...");
        m_Renderer = std::make_unique<Renderer>(m_Context);

        CH_CORE_INFO("Application: Initializing ImGui...");
        InitImGui();

        CH_CORE_INFO("Application: Initializing SceneRenderer...");
        m_SceneRenderer = std::make_unique<SceneRenderer>(m_Context, m_ResourceManager.get(), m_Renderer);

        CH_CORE_INFO("Application: Creating Scene...");
        m_Scene = std::make_shared<Scene>(m_Context, m_ResourceManager.get());

        CH_CORE_INFO("Application: Initializing RenderPath...");
        m_RenderPath = std::make_unique<HybridRenderPath>(m_Context, m_Scene, m_ResourceManager.get(), *m_PipelineManager, m_ResourceManager->GetGlobalDescriptorSetLayout());
        m_RenderPath->Init();

        m_LastFrameTime = (float)glfwGetTime();
        CH_CORE_INFO("Application initialized successfully.");
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

        // Dispatch to ImGui backends
        dispatcher.Dispatch<MouseButtonPressedEvent>([this](MouseButtonPressedEvent& e) {
            ImGui_ImplGlfw_MouseButtonCallback(m_Window->GetNativeWindow(), (int)e.GetMouseButton(), GLFW_PRESS, 0);
            return false;
        });
        dispatcher.Dispatch<MouseButtonReleasedEvent>([this](MouseButtonReleasedEvent& e) {
            ImGui_ImplGlfw_MouseButtonCallback(m_Window->GetNativeWindow(), (int)e.GetMouseButton(), GLFW_RELEASE, 0);
            return false;
        });
        dispatcher.Dispatch<MouseMovedEvent>([this](MouseMovedEvent& e) {
            // ImGui_ImplGlfw_CursorPosCallback usually queries the mouse position itself, 
            // but we can just let ImGui update its internal state during NewFrame.
            // Some versions of the backend don't even expose this publicly.
            return false;
        });
        dispatcher.Dispatch<MouseScrolledEvent>([this](MouseScrolledEvent& e) {
            ImGui_ImplGlfw_ScrollCallback(m_Window->GetNativeWindow(), e.GetXOffset(), e.GetYOffset());
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

        // Let ImGui handle events if it wants to
        ImGuiIO& io = ImGui::GetIO();
        
        bool isMouseScroll = e.GetEventType() == EventType::MouseScrolled;
        
        // We only block if ImGui wants the event AND it's not a scroll event 
        // (or we can let scroll through if it's over our viewport, handled in EditorLayer)
        e.Handled |= e.IsInCategory(EventCategoryMouse) & io.WantCaptureMouse && !isMouseScroll;
        e.Handled |= e.IsInCategory(EventCategoryKeyboard) & io.WantCaptureKeyboard;

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
        
        if (!m_Scene)
            m_Scene = std::make_shared<Scene>(m_Context, m_ResourceManager.get());
            
        m_Scene->LoadModel(path);
        if (m_RenderPath) m_RenderPath->SetScene(m_Scene);
    }

    void Application::ExecuteClearScene()
    {
        CH_CORE_INFO("Clearing scene.");
        vkDeviceWaitIdle(m_Context->GetDevice());
        m_Scene = std::make_shared<Scene>(m_Context, m_ResourceManager.get());
        if (m_RenderPath) m_RenderPath->SetScene(m_Scene);
    }

    void Application::cleanup()
    {
        vkDeviceWaitIdle(m_Context->GetDevice());
        for (auto& layer : m_LayerStack) layer->OnDetach();
        m_LayerStack.clear();
        m_RenderPath.reset();
        m_SceneRenderer.reset();
        ShutdownImGui();
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

    void Application::ClearScene()
    {
        QueueEvent([this]() { ExecuteClearScene(); });
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

    void Application::InitImGui()
    {
        // 1. Create Descriptor Pool
        VkDescriptorPoolSize pool_sizes[] =
        {
            { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
        };
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1000 * (uint32_t)std::size(pool_sizes);
        pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;

        if (vkCreateDescriptorPool(m_Context->GetDevice(), &pool_info, nullptr, &m_ImGuiDescriptorPool) != VK_SUCCESS)
            throw std::runtime_error("Failed to create ImGui descriptor pool");

        // 2. Initialize ImGui Context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

        ImGuiStyle& style = ImGui::GetStyle();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        ImGui::StyleColorsDark();
        SetImGuiDarkThemeColors();

        // 4. Initialize ImGui Backend
        ImGui_ImplGlfw_InitForVulkan(m_Window->GetNativeWindow(), false);
        
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = m_Context->GetInstance();
        init_info.PhysicalDevice = m_Context->GetPhysicalDevice();
        init_info.Device = m_Context->GetDevice();
        init_info.QueueFamily = m_Context->FindQueueFamilies(m_Context->GetPhysicalDevice()).graphicsFamily.value();
        init_info.Queue = m_Context->GetGraphicsQueue();
        init_info.DescriptorPool = m_ImGuiDescriptorPool;
        init_info.MinImageCount = MAX_FRAMES_IN_FLIGHT;
        init_info.ImageCount = MAX_FRAMES_IN_FLIGHT;
        init_info.UseDynamicRendering = true;
        
        VkPipelineRenderingCreateInfoKHR pipelineInfo = {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
        VkFormat colorFormat = m_Context->GetSwapChainImageFormat();
        pipelineInfo.colorAttachmentCount = 1;
        pipelineInfo.pColorAttachmentFormats = &colorFormat;
        
        init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.PipelineInfoMain.PipelineRenderingCreateInfo = pipelineInfo;

        ImGui_ImplVulkan_Init(&init_info);
    }

    void Application::ShutdownImGui()
    {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        if (m_ImGuiDescriptorPool)
            vkDestroyDescriptorPool(m_Context->GetDevice(), m_ImGuiDescriptorPool, nullptr);
    }

    void Application::BeginImGui()
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
    }

    void Application::EndImGui(VkCommandBuffer cmd, VkImageView targetView, VkExtent2D extent)
    {
        ImGui::Render();
        ImDrawData* drawData = ImGui::GetDrawData();

        VkRenderingAttachmentInfoKHR colorAttachment = {};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
        colorAttachment.imageView = targetView;
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        
        VkRenderingInfoKHR renderingInfo = {};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
        renderingInfo.renderArea = { {0, 0}, extent };
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;

        vkCmdBeginRendering(cmd, &renderingInfo);
        ImGui_ImplVulkan_RenderDrawData(drawData, cmd);
        vkCmdEndRendering(cmd);

        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
    }

    ImTextureID Application::GetImGuiTextureID(VkImageView view, VkSampler sampler)
    {
        if (view == VK_NULL_HANDLE) return (ImTextureID)0;
        if (sampler == VK_NULL_HANDLE) sampler = m_ResourceManager->GetDefaultSampler();
        if (m_ImGuiTextureCache.count(view)) return m_ImGuiTextureCache[view];

        ImTextureID id = (ImTextureID)ImGui_ImplVulkan_AddTexture(sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_ImGuiTextureCache[view] = id;
        return id;
    }

    void Application::ClearImGuiTextureCache() { m_ImGuiTextureCache.clear(); }

    void Application::SetImGuiDarkThemeColors()
    {
        auto& colors = ImGui::GetStyle().Colors;
        colors[ImGuiCol_WindowBg] = ImVec4{ 0.1f, 0.105f, 0.11f, 1.0f };
        colors[ImGuiCol_Header] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
        colors[ImGuiCol_HeaderHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
        colors[ImGuiCol_HeaderActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_Button] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
        colors[ImGuiCol_ButtonHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
        colors[ImGuiCol_ButtonActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_FrameBg] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
        colors[ImGuiCol_FrameBgHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
        colors[ImGuiCol_FrameBgActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_Tab] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_TabHovered] = ImVec4{ 0.38f, 0.3805f, 0.381f, 1.0f };
        colors[ImGuiCol_TabActive] = ImVec4{ 0.28f, 0.2805f, 0.281f, 1.0f };
        colors[ImGuiCol_TabUnfocused] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
        colors[ImGuiCol_TitleBg] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_TitleBgActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
    }
}