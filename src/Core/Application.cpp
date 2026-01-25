#include "pch.h"
#include "Application.h"
#include "Buffer.h"
#include "Image.h"
#include "FileIO.h"
#include "RayTracedRenderPath.h"
#include "ForwardRenderPath.h"
#include "HybridRenderPath.h"
#include "EditorLayer.h"
#include <imgui.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include "Random.h"

namespace Chimera {

    // ============ Barrier Helper Functions ============
    inline VkImageMemoryBarrier CreateImageBarrier(
        VkImage image,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        VkAccessFlags srcAccess,
        VkAccessFlags dstAccess,
        VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT
    ) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = dstAccess;
        barrier.subresourceRange.aspectMask = aspectMask;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        return barrier;
    }

    inline void CmdPipelineBarrier(
        VkCommandBuffer commandBuffer,
        VkPipelineStageFlags srcStage,
        VkPipelineStageFlags dstStage,
        const VkImageMemoryBarrier& barrier
    ) {
        vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    VkShaderModule LoadShaderModule(const std::string& filename, VkDevice device) {
        auto code = FileIO::ReadFile(filename);
        
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create shader module: " + filename);
        }
        return shaderModule;
    }

    Application::Application()
    {
        try {
            initWindow();
            initVulkan();
        } catch (const std::exception& e) {
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
        mainLoop();
    }

    void Application::initWindow()
    {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(Config::WINDOW_WIDTH, Config::WINDOW_HEIGHT, "Chimera Engine", nullptr, nullptr);
        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
        glfwSetKeyCallback(window, keyCallback);
        glfwSetMouseButtonCallback(window, mouseButtonCallback);
        glfwSetCursorPosCallback(window, cursorPosCallback);
        glfwSetScrollCallback(window, scrollCallback);
        glfwSetDropCallback(window, dropCallback);
    }

    void Application::framebufferResizeCallback(GLFWwindow* window, int width, int height)
    {
        auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
        app->framebufferResized = true;
    }

    void Application::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
    {
        auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
        if (app->m_CameraController)
            app->m_CameraController->OnKey(key, scancode, action, mods);
    }

    void Application::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
    {
        auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
        if (app->m_CameraController)
            app->m_CameraController->OnMouseButton(button, action, mods);
    }

    void Application::cursorPosCallback(GLFWwindow* window, double xpos, double ypos)
    {
        auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
        if (app->m_CameraController)
            app->m_CameraController->OnCursorPos(xpos, ypos);
    }

    void Application::scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
    {
        auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
        if (app->m_CameraController)
            app->m_CameraController->OnScroll(xoffset, yoffset);
    }

    void Application::dropCallback(GLFWwindow* window, int count, const char** paths)
    {
        auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
        if (count > 0) {
            app->LoadScene(std::string(paths[0]));
        }
    }

    void Application::initVulkan()
    {
        m_Context = std::make_shared<VulkanContext>(window);
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

        // Init ImGui Layer
        m_ImGuiLayer = std::make_shared<ImGuiLayer>(m_Context);
        PushLayer(m_ImGuiLayer);
        
        // Init Editor Layer
        PushLayer(std::make_shared<EditorLayer>(this));

        m_LastWindowExtent = m_Context->GetSwapChainExtent();
    }

    void Application::mainLoop()
    {
        while (!glfwWindowShouldClose(window))
        {
            float time = (float)glfwGetTime();
            float timestep = time - m_LastFrameTime;
            m_LastFrameTime = time;

            glfwPollEvents();

            if (m_CameraController)
                m_CameraController->OnUpdate(timestep);

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
        // Detach Layers (e.g. ImGui cleanup)
        for (auto& layer : m_LayerStack) {
            layer->OnDetach();
        }
        m_LayerStack.clear();

        cleanupSwapChain();

        // RenderPath cleanup
        m_RenderPath.reset();

        // Sync Objects cleanup handled by Renderer destructor

        // vkDestroyCommandPool(m_Context->GetDevice(), commandPool, nullptr); // Managed by Context

        glfwDestroyWindow(window);
        glfwTerminate();
    }

    VkShaderModule Application::createShaderModule(const std::vector<char>& code)
    {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(m_Context->GetDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create shader module!");
        }
        return shaderModule;
    }

    VkCommandBuffer Application::beginSingleTimeCommands()
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = m_Context->GetCommandPool();
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(m_Context->GetDevice(), &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        return commandBuffer;
    }

    void Application::endSingleTimeCommands(VkCommandBuffer commandBuffer)
    {
        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        vkQueueSubmit(m_Context->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_Context->GetGraphicsQueue());

        vkFreeCommandBuffers(m_Context->GetDevice(), m_Context->GetCommandPool(), 1, &commandBuffer);
    }

    void Application::cleanupSwapChain()
    {
        // Resources dependent on swapchain size are now managed by RenderPath
        // and cleaned up via RenderPath::OnResize or destructor.
    }

    void Application::updateUniformBuffer(uint32_t currentImage)
    {
        static auto startTime = std::chrono::high_resolution_clock::now();
        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

        // Retrieve Camera from Scene
        auto& camera = m_Scene->GetCamera();
        
        // Simple orbiting camera update (to keep the dynamic behavior for now, or we can remove it if Scene has a controller)
        // For now, let's update the scene camera here to match the previous behavior
        // In a real engine, a CameraController system would do this update in OnUpdate().
        // camera.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        // camera.proj = glm::perspective(glm::radians(45.0f), m_Context->GetSwapChainExtent().width / (float)m_Context->GetSwapChainExtent().height, 0.1f, 10.0f);
        // camera.proj[1][1] *= -1;

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
        if (m_RenderPathSwitchPending) {
            ExecuteRenderPathSwitch(m_PendingRenderPathType);
            m_RenderPathSwitchPending = false;
        }

        if (m_SceneLoadPending) {
            ExecuteLoadScene(m_PendingScenePath);
            m_SceneLoadPending = false;
        }

        VkCommandBuffer commandBuffer = m_Renderer->BeginFrame();

        // Check for resize regardless of whether we are drawing this frame (BeginFrame might have recreated swapchain)
        auto newExtent = m_Context->GetSwapChainExtent();
        if (newExtent.width != m_LastWindowExtent.width || newExtent.height != m_LastWindowExtent.height) {
            m_RenderPath->OnResize(newExtent.width, newExtent.height);
            for (auto& layer : m_LayerStack) {
                layer->OnResize(newExtent.width, newExtent.height);
            }
            m_LastWindowExtent = newExtent;
        }

        if (commandBuffer == VK_NULL_HANDLE) return; // Swapchain reconstruction or minimized

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

        if (m_RenderPath) {
            m_RenderPath->Init();
            // Ensure render path knows about current window size
            VkExtent2D extent = m_Context->GetSwapChainExtent();
            m_RenderPath->OnResize(extent.width, extent.height);
        }
        
        m_CurrentRenderPathType = type;
    }

    void Application::LoadScene(const std::string& path)
    {
        m_PendingScenePath = path;
        m_SceneLoadPending = true;
    }

    void Application::ExecuteLoadScene(const std::string& path)
    {
        vkDeviceWaitIdle(m_Context->GetDevice());
        
        try {
            m_Scene->LoadModel(path);
            
            // Re-initialize RenderPath dependencies
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

        m_RenderPath->Render(commandBuffer, currentFrame, imageIndex, m_ResourceManager->GetGlobalDescriptorSet(currentFrame), swapChainImages,
            [this, imageIndex](VkCommandBuffer cmd) {
                m_ImGuiLayer->Begin();

                // Render Layers UI (includes EditorLayer)
                for (auto& layer : m_LayerStack)
                    layer->OnUIRender();

                m_ImGuiLayer->End(cmd, imageIndex);
            }
        );
    }

    void Application::transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels)
    {
        // Optimized version: submits immediately without queue wait
        transitionImageLayoutImmediate(image, format, oldLayout, newLayout, mipLevels);
    }

    void Application::transitionImageLayoutImmediate(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels)
    {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags sourceStage, destinationStage;
        if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        {
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            if (format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT)
            {
                barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            }
        }
        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; 
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_GENERAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
        }
        else
        {
            throw std::invalid_argument("unsupported layout transition!");
        }
        vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        endSingleTimeCommands(commandBuffer);
    }

    void Application::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
    {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();
        VkBufferCopy copyRegion{};
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
        endSingleTimeCommands(commandBuffer);
    }
}
