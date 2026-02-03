#include "pch.h"
#include "core/application/Application.h"
#include "core/application/Input.h"
#include "core/utilities/FileIO.h"
#include "core/utilities/Random.h"
#include "gfx/utils/VulkanBarrier.h"
#include "gfx/utils/VulkanShaderUtils.h"
#include "gfx/utils/VulkanDescriptorUtils.h"
#include "gfx/utils/VulkanScreenshot.h"

#include "gfx/resources/Buffer.h"
#include "gfx/resources/Image.h"

#include "rendering/pipelines/RayTracedRenderPath.h"
#include "rendering/pipelines/ForwardRenderPath.h"
#include "rendering/pipelines/HybridRenderPath.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"


namespace Chimera {

    Application* Application::s_Instance = nullptr;

    Application::Application(const ApplicationSpecification& spec)
        : m_Specification(spec), 
          m_EditorCamera(45.0f, (float)spec.Width / (float)spec.Height, 0.1f, 1000.0f)
    {
        s_Instance = this;
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

    void Application::initWindow()
    {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        m_Window = glfwCreateWindow(m_Specification.Width, m_Specification.Height, m_Specification.Name.c_str(), nullptr, nullptr);
        glfwSetWindowUserPointer(m_Window, this);
        glfwSetFramebufferSizeCallback(m_Window, framebufferResizeCallback);
        glfwSetScrollCallback(m_Window, scrollCallback);
    }

    void Application::initVulkan()
    {
        m_Context = std::make_shared<VulkanContext>(m_Window);
        m_Renderer = std::make_shared<Renderer>(m_Context);

        m_ResourceManager = std::make_unique<ResourceManager>(m_Context);
        m_ResourceManager->InitGlobalResources(); 

        m_Scene = std::make_shared<Scene>(m_Context, m_ResourceManager.get());
        std::string modelPath = "assets/models/fantasy_queen/scene.gltf";
        CH_CORE_INFO("Loading model from: {}", std::filesystem::absolute(modelPath).string());
        m_Scene->LoadModel(modelPath); 
        
        m_Scene->GetLight().direction = glm::vec4(0.5f, -1.0f, 0.2f, 0.0f);
        m_Scene->GetLight().color = glm::vec4(1.0f, 1.0f, 1.0f, 5.0f);

        // Synchronize Editor Camera with actual swapchain extent
        auto extent = m_Context->GetSwapChainExtent();
        m_EditorCamera.SetViewportSize((float)extent.width, (float)extent.height);

        m_ImGuiLayer = std::make_unique<ImGuiLayer>(m_Context);
        m_ImGuiLayer->OnAttach();

        // 默认使用 HybridRenderPath
        m_RenderPath = std::make_unique<HybridRenderPath>(m_Context, m_Scene, m_ResourceManager.get(), m_ResourceManager->GetGlobalDescriptorSetLayout());
        
        m_RenderPath->Init();
    }

    void Application::Run()
    {
        while (!glfwWindowShouldClose(m_Window))
        {
            glfwPollEvents();
            
            if (m_FramebufferResized)
            {
                int width = 0, height = 0;
                glfwGetFramebufferSize(m_Window, &width, &height);
                while (width == 0 || height == 0) {
                    glfwGetFramebufferSize(m_Window, &width, &height);
                    glfwWaitEvents();
                }

                vkDeviceWaitIdle(m_Context->GetDevice());
                m_Context->RecreateSwapChain();
                
                uint32_t w = m_Context->GetSwapChainExtent().width;
                uint32_t h = m_Context->GetSwapChainExtent().height;

                m_EditorCamera.SetViewportSize((float)w, (float)h);
                
                m_FramebufferResized = false;
            }

            float currentFrameTime = (float)glfwGetTime();
            float dt = currentFrameTime - m_LastFrameTime;
            m_LastFrameTime = currentFrameTime;

            // Camera Update
            m_EditorCamera.OnUpdate(dt);

            drawFrame();
        }
        vkDeviceWaitIdle(m_Context->GetDevice());
    }

    void Application::drawFrame()
    {
        VkCommandBuffer commandBuffer = m_Renderer->BeginFrame();
        if (commandBuffer == VK_NULL_HANDLE) return; 

        uint32_t imageIndex = m_Renderer->GetCurrentImageIndex();
        uint32_t currentFrame = m_Renderer->GetCurrentFrameIndex();
        
        const auto& swapChainImages = m_Context->GetSwapChainImages();

        m_ResourceManager->UpdateGlobalResources(currentFrame, {
            glm::mat4(1.0f), // Model (Identity)
            m_EditorCamera.GetViewMatrix(),
            m_EditorCamera.GetProjection(),
            m_EditorCamera.GetProjection(), // prevView (TODO)
            m_EditorCamera.GetProjection(), // prevProj (TODO)
            m_Scene->GetLight().direction, // Light Pos (using direction for now)
            (int)currentFrame
        });

        m_RenderPath->Render(commandBuffer, currentFrame, imageIndex, m_ResourceManager->GetGlobalDescriptorSet(currentFrame), swapChainImages,
            [this, imageIndex](VkCommandBuffer cmd)
            {
                m_ImGuiLayer->Begin();
                
                // Update blocking status for NEXT frame
                m_ImGuiLayer->BlockEvents(ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard);

                ImGui::Begin("Settings");
                ImGui::Text("Application Average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
                m_RenderPath->OnImGui();
                
                ImGui::Separator();
                ImGui::Text("Camera");
                ImGui::Text("Pos: %.2f, %.2f, %.2f", m_EditorCamera.GetPosition().x, m_EditorCamera.GetPosition().y, m_EditorCamera.GetPosition().z);
                ImGui::Text("Pitch: %.2f, Yaw: %.2f", m_EditorCamera.GetPitch(), m_EditorCamera.GetYaw());
                ImGui::Text("Distance: %.2f", m_EditorCamera.GetDistance());

                ImGui::End();

                m_ImGuiLayer->End(cmd, m_Context->GetSwapChainImageViews()[imageIndex], m_Context->GetSwapChainExtent());
            }
        );

        m_Renderer->EndFrame();
    }

    void Application::cleanup()
    {
        vkDeviceWaitIdle(m_Context->GetDevice());

        for (auto& layer : m_LayerStack)
            layer->OnDetach();
        m_LayerStack.clear();

        m_RenderPath.reset();
        m_ImGuiLayer.reset();
        m_Scene.reset();
        m_ResourceManager.reset();
        m_Renderer.reset();
        m_Context.reset(); 
        glfwDestroyWindow(m_Window);
        glfwTerminate();
    }

    void Application::framebufferResizeCallback(GLFWwindow* window, int width, int height)
    {
        auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
        app->m_FramebufferResized = true;
    }

    void Application::scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
    {
        auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
        if (!app->m_ImGuiLayer->BlockEvents()) {
            app->m_EditorCamera.OnEvent((float)yoffset);
        }
    }

    // Callbacks below unused or default implementations
    void Application::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {}
    void Application::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {}
    void Application::cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {}
    void Application::dropCallback(GLFWwindow* window, int count, const char** paths) {}

    void Application::PushLayer(const std::shared_ptr<Layer>& layer)
    {
        m_LayerStack.push_back(layer);
        layer->OnAttach();
    }

    void Application::SwitchRenderPath(RenderPathType type)
    {
        m_PendingRenderPathType = type;
        m_RenderPathSwitchPending = true;
    }

    void Application::LoadScene(const std::string& path)
    {
        m_PendingScenePath = path;
        m_SceneLoadPending = true;
    }

    void Application::Close()
    {
        glfwSetWindowShouldClose(m_Window, GLFW_TRUE);
    }

    void Application::ExecuteRenderPathSwitch(RenderPathType type)
    {
        vkDeviceWaitIdle(m_Context->GetDevice());
        m_RenderPath.reset();
        m_CurrentRenderPathType = type;

        switch (type)
        {
        case RenderPathType::Forward:
            m_RenderPath = std::make_unique<ForwardRenderPath>(m_Context, m_Scene, m_ResourceManager.get(), m_ResourceManager->GetGlobalDescriptorSetLayout());
            break;
        case RenderPathType::Hybrid:
            m_RenderPath = std::make_unique<HybridRenderPath>(m_Context, m_Scene, m_ResourceManager.get(), m_ResourceManager->GetGlobalDescriptorSetLayout());
            break;
        case RenderPathType::RayTracing:
             m_RenderPath = std::make_unique<RayTracedRenderPath>(m_Context, m_Scene, m_ResourceManager.get(), m_ResourceManager->GetGlobalDescriptorSetLayout());
             break;
        }
        
        if (m_RenderPath)
            m_RenderPath->Init();
    }

    void Application::ExecuteLoadScene(const std::string& path)
    {
        vkDeviceWaitIdle(m_Context->GetDevice());
        m_Scene->LoadModel(path);
    }
}