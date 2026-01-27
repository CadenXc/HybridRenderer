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
#include "rendering/pipelines/HybridRenderPath.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

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
        
        // Use specification for m_Window creation
        if (m_Specification.WindowResizeable)
        {
            glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        }
        else
        {
            glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        }

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
        if (app->m_CameraController)
        {
            app->m_CameraController->OnKey(key, scancode, action, mods);
        }
    }

    void Application::mouseButtonCallback(GLFWwindow* m_Window, int button, int action, int mods)
    {
        auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(m_Window));
        if (app->m_CameraController)
        {
            app->m_CameraController->OnMouseButton(button, action, mods);
        }
    }

    void Application::cursorPosCallback(GLFWwindow* m_Window, double xpos, double ypos)
    {
        auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(m_Window));
        if (app->m_CameraController)
        {
            app->m_CameraController->OnCursorPos(xpos, ypos);
        }
    }

    void Application::scrollCallback(GLFWwindow* m_Window, double xoffset, double yoffset)
    {
        auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(m_Window));
        if (app->m_CameraController)
        {
            app->m_CameraController->OnScroll(xoffset, yoffset);
        }
    }

    void Application::dropCallback(GLFWwindow* m_Window, int count, const char** paths)
    {
        auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(m_Window));
        if (count > 0)
        {
            app->LoadScene(std::string(paths[0]));
        }
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

        // Global Resources (UBOs, Descriptors, Texture)
        m_ResourceManager->InitGlobalResources();

        // Initialize Render Path
        ExecuteRenderPathSwitch(RenderPathType::Forward); // Default to Forward

        // Init ImGui
        InitImGui();
        
        PushLayer(std::make_shared<EditorLayer>(this));

        m_LastWindowExtent = m_Context->GetSwapChainExtent();
    }

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
        pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
        pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;

        if (vkCreateDescriptorPool(m_Context->GetDevice(), &pool_info, nullptr, &m_ImGuiDescriptorPool) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create ImGui descriptor pool");
        }

        // 2. Initialize ImGui Context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking!
        // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;      // Viewports (optional)

        // 3. Set Style
        ImGui::StyleColorsDark();
        SetDarkThemeColors();

        // 4. Create RenderPass and Framebuffers
        CreateImGuiRenderPass();
        CreateImGuiFramebuffers();

        // 5. Initialize ImGui Backend
        ImGui_ImplGlfw_InitForVulkan(m_Window, true);
        
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = m_Context->GetInstance();
        init_info.PhysicalDevice = m_Context->GetPhysicalDevice();
        init_info.Device = m_Context->GetDevice();
        init_info.QueueFamily = m_Context->FindQueueFamilies(m_Context->GetPhysicalDevice()).graphicsFamily.value();
        init_info.Queue = m_Context->GetGraphicsQueue();
        init_info.PipelineCache = VK_NULL_HANDLE;
        init_info.DescriptorPool = m_ImGuiDescriptorPool;
        init_info.MinImageCount = Config::MAX_FRAMES_IN_FLIGHT;
        init_info.ImageCount = Config::MAX_FRAMES_IN_FLIGHT;
        init_info.Allocator = nullptr;
        init_info.PipelineInfoMain.RenderPass = m_ImGuiRenderPass;

        ImGui_ImplVulkan_Init(&init_info);
        
        // Font upload is handled automatically or via NewFrame in newer versions?
        // If compilation fails, we can assume we don't need to call CreateFontsTexture manually
        // or we need to find the correct function.
        // For now, let's remove the manual call and rely on Init/NewFrame.
    }

    void Application::ShutdownImGui()
    {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        for (auto fb : m_ImGuiFramebuffers)
        {
            vkDestroyFramebuffer(m_Context->GetDevice(), fb, nullptr);
        }
        
        if (m_ImGuiRenderPass)
        {
            vkDestroyRenderPass(m_Context->GetDevice(), m_ImGuiRenderPass, nullptr);
        }
        
        if (m_ImGuiDescriptorPool)
        {
            vkDestroyDescriptorPool(m_Context->GetDevice(), m_ImGuiDescriptorPool, nullptr);
        }
    }

    void Application::CreateImGuiRenderPass()
    {
        VkAttachmentDescription attachment = {};
        attachment.format = m_Context->GetSwapChainImageFormat();
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // Keep content from previous pass
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference color_attachment = {};
        color_attachment.attachment = 0;
        color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_attachment;

        VkSubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = 1;
        info.pAttachments = &attachment;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 1;
        info.pDependencies = &dependency;

        if (vkCreateRenderPass(m_Context->GetDevice(), &info, nullptr, &m_ImGuiRenderPass) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create ImGui RenderPass");
        }
    }

    void Application::CreateImGuiFramebuffers()
    {
        // Clean up existing
        for (auto fb : m_ImGuiFramebuffers)
        {
            vkDestroyFramebuffer(m_Context->GetDevice(), fb, nullptr);
        }
        m_ImGuiFramebuffers.clear();

        const auto& imageViews = m_Context->GetSwapChainImageViews();
        VkExtent2D extent = m_Context->GetSwapChainExtent();
        
        m_ImGuiFramebuffers.resize(imageViews.size());

        for (size_t i = 0; i < imageViews.size(); i++)
        {
            VkImageView attachment[] = { imageViews[i] };
            VkFramebufferCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            info.renderPass = m_ImGuiRenderPass;
            info.attachmentCount = 1;
            info.pAttachments = attachment;
            info.width = extent.width;
            info.height = extent.height;
            info.layers = 1;

            if (vkCreateFramebuffer(m_Context->GetDevice(), &info, nullptr, &m_ImGuiFramebuffers[i]) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create ImGui Framebuffer");
            }
        }
    }

    void Application::SetDarkThemeColors()
    {
        auto& colors = ImGui::GetStyle().Colors;
        colors[ImGuiCol_WindowBg] = ImVec4{ 0.1f, 0.105f, 0.11f, 1.0f };

        // Headers
        colors[ImGuiCol_Header] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
        colors[ImGuiCol_HeaderHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
        colors[ImGuiCol_HeaderActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        
        // Buttons
        colors[ImGuiCol_Button] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
        colors[ImGuiCol_ButtonHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
        colors[ImGuiCol_ButtonActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

        // Frame BG
        colors[ImGuiCol_FrameBg] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
        colors[ImGuiCol_FrameBgHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
        colors[ImGuiCol_FrameBgActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

        // Tabs
        colors[ImGuiCol_Tab] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_TabHovered] = ImVec4{ 0.38f, 0.3805f, 0.381f, 1.0f };
        colors[ImGuiCol_TabActive] = ImVec4{ 0.28f, 0.2805f, 0.281f, 1.0f };
        colors[ImGuiCol_TabUnfocused] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };

        // Title
        colors[ImGuiCol_TitleBg] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_TitleBgActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
    }

    void Application::BeginImGuiFrame()
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        
        // DockSpace
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
    }

    void Application::EndImGuiFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        ImGui::Render();

        VkRenderPassBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        info.renderPass = m_ImGuiRenderPass;
        info.framebuffer = m_ImGuiFramebuffers[imageIndex];
        info.renderArea.extent = m_Context->GetSwapChainExtent();
        info.clearValueCount = 0;
        info.pClearValues = nullptr;

        vkCmdBeginRenderPass(commandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
        vkCmdEndRenderPass(commandBuffer);
        
        // Update and Render additional Platform Windows (if Viewports enabled)
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
    }

    void Application::mainLoop()
    {
        while (!glfwWindowShouldClose(m_Window))
        {
            float time = (float)glfwGetTime();
            float timestep = time - m_LastFrameTime;
            m_LastFrameTime = time;

            glfwPollEvents();

            if (m_CameraController)
            {
                m_CameraController->OnUpdate(timestep);
            }

            for (auto& layer : m_LayerStack)
            {
                layer->OnUpdate(timestep);
            }

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
        vkDeviceWaitIdle(m_Context->GetDevice()); // Wait before cleanup

        // Detach Layers
        for (auto& layer : m_LayerStack)
        {
            layer->OnDetach();
        }
        m_LayerStack.clear();

        ShutdownImGui();

        cleanupSwapChain();

        // RenderPath cleanup
        m_RenderPath.reset();

        glfwDestroyWindow(m_Window);
        glfwTerminate();
    }

    // Removed createShaderModule, beginSingleTimeCommands, endSingleTimeCommands, copyBuffer, transitionImageLayout etc.
    // They are now in VulkanUtils.

    void Application::cleanupSwapChain()
    {
        // Resources dependent on swapchain size are now managed by RenderPath
    }

    void Application::updateUniformBuffer(uint32_t currentImage)
    {
        // ... same as before
        static auto startTime = std::chrono::high_resolution_clock::now();
        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

        // Retrieve Camera from Scene
        auto& camera = m_Scene->GetCamera();
        
        UniformBufferObject ubo{};
        ubo.model = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.view = camera.view;
        ubo.proj = camera.proj;
        
        // Use Scene Light
        auto& light = m_Scene->GetLight();
        // Update light position for animation effect
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
            // Recreate ImGui Framebuffers on resize
            CreateImGuiFramebuffers();

            for (auto& layer : m_LayerStack)
            {
                layer->OnResize(newExtent.width, newExtent.height);
            }
            m_LastWindowExtent = newExtent;
        }

        if (commandBuffer == VK_NULL_HANDLE)
        {
            return;
        }

        if (!m_RenderPath)
        {
            return;
        }

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
        case RenderPathType::Hybrid:
            CH_CORE_INFO("Switching to Hybrid Render Path");
            m_RenderPath = std::make_unique<HybridRenderPath>(m_Context, m_Scene, m_ResourceManager.get(), m_ResourceManager->GetGlobalDescriptorSetLayout());
            break;
        }

        if (m_RenderPath)
        {
            try
            {
                m_RenderPath->Init();
                m_CurrentRenderPathType = type;
            }
            catch (const std::exception& e)
            {
                CH_CORE_ERROR("Failed to initialize render path: {}", e.what());
                m_RenderPath.reset();
                // Revert to Forward if we weren't already trying to switch to it
                if (type != RenderPathType::Forward)
                {
                    ExecuteRenderPathSwitch(RenderPathType::Forward);
                }
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
        
        try
        {
            m_Scene->LoadModel(path);
            m_RenderPath->OnSceneUpdated();
            CH_CORE_INFO("Loaded scene: {}", path);
        }
        catch (const std::exception& e)
        {
            CH_CORE_ERROR("Failed to load scene '{}': {}", path, e.what());
        }
    }

    void Application::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        const auto& swapChainImages = m_Context->GetSwapChainImages();
        uint32_t currentFrame = m_Renderer->GetCurrentFrameIndex();

        m_RenderPath->Render(commandBuffer, currentFrame, imageIndex, m_ResourceManager->GetGlobalDescriptorSet(currentFrame), swapChainImages,
            [this, imageIndex](VkCommandBuffer cmd)
            {
                BeginImGuiFrame();

                for (auto& layer : m_LayerStack)
                {
                    layer->OnUIRender();
                }

                EndImGuiFrame(cmd, imageIndex);
            }
        );
    }
}




