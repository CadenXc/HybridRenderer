#include "pch.h"
#include "core/application/Application.h"
#include "core/utilities/FileIO.h"
#include "core/utilities/Random.h"
#include "gfx/utils/VulkanBarrier.h"
#include "gfx/utils/VulkanShaderUtils.h"
#include "gfx/utils/VulkanDescriptorUtils.h"

#include "gfx/resources/Buffer.h"
#include "gfx/resources/Image.h"
#include "editor/EditorLayer.h"

#include "rendering/pipelines/RayTracedRenderPath.h"
#include "rendering/pipelines/ForwardRenderPath.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"


namespace Chimera {

    Application::Application(const ApplicationSpecification& spec)
        : m_Specification(spec)
    {
        try
        {
            initWindow();
            initVulkan();
        }
        catch (const std::exception& e)
        {
            CH_CORE_ERROR("Application::Application - Exception during initialization: {}", e.what());
            throw;
        }
    }

    Application::~Application()
    {
        cleanup();
    }

    void Application::Run()
    {
        static bool isRunning = false;
        if (isRunning) return;
        isRunning = true;

        mainLoop();
        
        isRunning = false;
    }

    void Application::initWindow()
    {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        if (m_Specification.WindowResizeable) glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        else glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        m_Window = glfwCreateWindow(m_Specification.Width, m_Specification.Height, m_Specification.Name.c_str(), nullptr, nullptr);
        glfwSetWindowUserPointer(m_Window, this);
        glfwSetFramebufferSizeCallback(m_Window, framebufferResizeCallback);
        glfwSetKeyCallback(m_Window, keyCallback);
        glfwSetMouseButtonCallback(m_Window, mouseButtonCallback);
        glfwSetCursorPosCallback(m_Window, cursorPosCallback);
        glfwSetScrollCallback(m_Window, scrollCallback);
        glfwSetDropCallback(m_Window, dropCallback);
    }

    void Application::framebufferResizeCallback(GLFWwindow* m_Window, int width, int height)
    {
        auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(m_Window));
        app->m_FramebufferResized = true;
    }

    void Application::keyCallback(GLFWwindow* m_Window, int key, int scancode, int action, int mods)
    {
        auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(m_Window));
        if (app->m_CameraController) app->m_CameraController->OnKey(key, scancode, action, mods);
    }

    void Application::mouseButtonCallback(GLFWwindow* m_Window, int button, int action, int mods)
    {
        auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(m_Window));
        if (app->m_CameraController) app->m_CameraController->OnMouseButton(button, action, mods);
    }

    void Application::cursorPosCallback(GLFWwindow* m_Window, double xpos, double ypos)
    {
        auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(m_Window));
        if (app->m_CameraController) app->m_CameraController->OnCursorPos(xpos, ypos);
    }

    void Application::scrollCallback(GLFWwindow* m_Window, double xoffset, double yoffset)
    {
        auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(m_Window));
        if (app->m_CameraController) app->m_CameraController->OnScroll(xoffset, yoffset);
    }

    void Application::dropCallback(GLFWwindow* m_Window, int count, const char** paths)
    {
        auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(m_Window));
        if (count > 0) app->LoadScene(std::string(paths[0]));
    }

    void Application::initVulkan()
    {
        m_Context = std::make_shared<VulkanContext>(m_Window);
        m_Renderer = std::make_shared<Renderer>(m_Context);
        m_ResourceManager = std::make_unique<ResourceManager>(m_Context);

        m_Scene = std::make_shared<Scene>(m_Context);
        m_Scene->LoadModel(std::string(Config::MODEL_PATH));
        
        m_CameraController = std::make_unique<CameraController>();
        m_CameraController->SetCamera(&m_Scene->GetCamera());

        m_ResourceManager->InitGlobalResources();

        ExecuteRenderPathSwitch(RenderPathType::Forward); 

        // Init ImGui Layer
        m_ImGuiLayer = std::make_shared<ImGuiLayer>(m_Context);
        PushLayer(m_ImGuiLayer);
        
        PushLayer(std::make_shared<EditorLayer>(this));

        m_LastWindowExtent = m_Context->GetSwapChainExtent();
    }

    void Application::mainLoop()
    {
        while (!glfwWindowShouldClose(m_Window))
        {
            float time = (float)glfwGetTime();
            float timestep = time - m_LastFrameTime;
            m_LastFrameTime = time;

            glfwPollEvents();

            if (m_CameraController) m_CameraController->OnUpdate(timestep);

            for (auto& layer : m_LayerStack) layer->OnUpdate(timestep);

            drawFrame();
        }
        vkDeviceWaitIdle(m_Context->GetDevice());
    }

    void Application::PushLayer(const std::shared_ptr<Layer>& layer)
    {
        m_LayerStack.push_back(layer);
        layer->OnAttach();
    }

    void Application::cleanup()
    {
        vkDeviceWaitIdle(m_Context->GetDevice()); 

        for (auto& layer : m_LayerStack) layer->OnDetach();
        m_LayerStack.clear();
        
        m_ImGuiLayer.reset(); // Cleanup ImGui

        cleanupSwapChain();
        m_RenderPath.reset();

        glfwDestroyWindow(m_Window);
        glfwTerminate();
    }

    void Application::cleanupSwapChain()
    {
    }

    void Application::updateUniformBuffer(uint32_t currentImage)
    {
        static auto startTime = std::chrono::high_resolution_clock::now();
        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

        auto& camera = m_Scene->GetCamera();
        UniformBufferObject ubo{};
        ubo.model = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.view = camera.view;
        ubo.proj = camera.proj;
        
        auto& light = m_Scene->GetLight();
        light.position = glm::vec4(2.0f * sin(time), 4.0f, 2.0f * cos(time), (float)Random::UInt(0, 100000));
        ubo.lightPos = light.position;
        
        m_ResourceManager->UpdateGlobalResources(currentImage, ubo);
    }

    void Application::drawFrame()
    {
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

        VkCommandBuffer commandBuffer = m_Renderer->BeginFrame();

        auto newExtent = m_Context->GetSwapChainExtent();
        if (newExtent.width != m_LastWindowExtent.width || newExtent.height != m_LastWindowExtent.height)
        {
            for (auto& layer : m_LayerStack) layer->OnResize(newExtent.width, newExtent.height);
            m_LastWindowExtent = newExtent;
        }

        if (commandBuffer == VK_NULL_HANDLE) return;
        if (!m_RenderPath) return;

        uint32_t imageIndex = m_Renderer->GetCurrentImageIndex();
        uint32_t frameIndex = m_Renderer->GetCurrentFrameIndex();

        updateUniformBuffer(frameIndex);

        recordCommandBuffer(commandBuffer, imageIndex);

        m_Renderer->EndFrame();
    }

    void Application::SwitchRenderPath(RenderPathType type)
    {
        m_PendingRenderPathType = type;
        m_RenderPathSwitchPending = true;
    }

    void Application::ExecuteRenderPathSwitch(RenderPathType type)
    {
        vkDeviceWaitIdle(m_Context->GetDevice());
        m_RenderPath.reset();

        switch (type)
        {
        case RenderPathType::Forward:
            CH_CORE_INFO("Switching to Forward Render Path");
            m_RenderPath = std::make_unique<ForwardRenderPath>(m_Context, m_Scene, m_ResourceManager.get(), m_ResourceManager->GetGlobalDescriptorSetLayout());
            break;
        case RenderPathType::RayTracing:
            CH_CORE_INFO("Switching to Ray Tracing Render Path");
            m_RenderPath = std::make_unique<RayTracedRenderPath>(m_Context, m_Scene, m_ResourceManager.get(), m_ResourceManager->GetGlobalDescriptorSetLayout());
            break;
        }

        if (m_RenderPath)
        {
            try {
                m_RenderPath->Init();
                m_CurrentRenderPathType = type;
            } catch (const std::exception& e) {
                CH_CORE_ERROR("Failed to initialize render path: {}", e.what());
                m_RenderPath.reset();
                if (type != RenderPathType::Forward) ExecuteRenderPathSwitch(RenderPathType::Forward);
            }
        }
    }

    void Application::LoadScene(const std::string& path)
    {
        m_PendingScenePath = path;
        m_SceneLoadPending = true;
    }

    void Application::Close()
    {
        glfwSetWindowShouldClose(m_Window, true);
    }

    void Application::ExecuteLoadScene(const std::string& path)
    {
        vkDeviceWaitIdle(m_Context->GetDevice());
        try {
            m_Scene->LoadModel(path);
            m_RenderPath->OnSceneUpdated();
            CH_CORE_INFO("Loaded scene: {}", path);
        } catch (const std::exception& e) {
            CH_CORE_ERROR("Failed to load scene '{}': {}", path, e.what());
        }
    }

    void Application::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        const auto& swapChainImages = m_Context->GetSwapChainImages();
        uint32_t currentFrame = m_Renderer->GetCurrentFrameIndex();

        // Render Scene (RenderPath)
        // Note: RenderPath now handles transitioning the swapchain image to COLOR_ATTACHMENT_OPTIMAL (or similar) if it renders to it.
        // Or for Hybrid, it transitions to TRANSFER_DST then blits.
        // We need to know what state the image is in before ImGui renders.
        // ImGuiLayer::End expects COLOR_ATTACHMENT_OPTIMAL.
        
        m_RenderPath->Render(commandBuffer, currentFrame, imageIndex, m_ResourceManager->GetGlobalDescriptorSet(currentFrame), swapChainImages,
            [this, imageIndex](VkCommandBuffer cmd)
            {
                // UI Callback
                m_ImGuiLayer->Begin();
                for (auto& layer : m_LayerStack) layer->OnUIRender();
                
                // ImGuiLayer uses Dynamic Rendering. It needs the target View.
                m_ImGuiLayer->End(cmd, m_Context->GetSwapChainImageViews()[imageIndex], m_Context->GetSwapChainExtent());
            }
        );
    }
}