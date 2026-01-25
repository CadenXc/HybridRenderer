#pragma once

#include "pch.h"
#include "VulkanContext.h"
#include "Renderer.h"
#include "RenderPath.h"
#include "Scene.h"
#include "Layer.h"
#include "ImGuiLayer.h"
#include "Buffer.h"
#include "Image.h"
#include "CameraController.h"
#include "ResourceManager.h"
#include "Config.h"

namespace Chimera {

    class Buffer;
    class Image;
    // Vertex struct moved to Scene.h

    enum class RenderPathType {
        Forward,
        RayTracing,
        Hybrid
    };

    class Application {
    public:
        Application();
        virtual ~Application();

        void Run();
        void PushLayer(const std::shared_ptr<Layer>& layer);
        void SwitchRenderPath(RenderPathType type);
        void LoadScene(const std::string& path);

        // Expose Context and Renderer to Layers (for now, until we abstract further)
        std::shared_ptr<VulkanContext> GetContext() { return m_Context; }
        std::shared_ptr<Renderer> GetRenderer() { return m_Renderer; }
        RenderPath* GetRenderPath() { return m_RenderPath.get(); }
        RenderPathType GetCurrentRenderPathType() const { return m_CurrentRenderPathType; }
        GLFWwindow* GetWindow() { return window; }

    private:
        void initWindow();
        void initVulkan();
        void mainLoop();
        void cleanup();

        void cleanupSwapChain();
        
        // void createFrameResources(); // Moved to Renderer
        // void createCommandBuffers();
        // void createSyncObjects();

        // Ray Tracing

        void drawFrame();
        void updateUniformBuffer(uint32_t currentImage);
        void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);

        VkShaderModule createShaderModule(const std::vector<char>& code);
        
        bool hasStencilComponent(VkFormat format);
        VkCommandBuffer beginSingleTimeCommands();
        void endSingleTimeCommands(VkCommandBuffer commandBuffer);
        
        // 优化版本：用于初始化阶段批量提交 Barriers
        void transitionImageLayoutImmediate(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels);
        
        void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
        void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels);
        void ExecuteRenderPathSwitch(RenderPathType type);
        void ExecuteLoadScene(const std::string& path);

        uint32_t align_up(uint32_t value, uint32_t alignment);

        static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
        static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
        static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
        static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
        static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
        static void dropCallback(GLFWwindow* window, int count, const char** paths);

    private:
        GLFWwindow* window;
        std::shared_ptr<VulkanContext> m_Context;
        std::shared_ptr<Renderer> m_Renderer;
        std::shared_ptr<Scene> m_Scene;
        std::unique_ptr<CameraController> m_CameraController;
        std::unique_ptr<ResourceManager> m_ResourceManager;
        std::unique_ptr<RenderPath> m_RenderPath;
        RenderPathType m_CurrentRenderPathType = RenderPathType::Forward;
        
        bool m_RenderPathSwitchPending = false;
        RenderPathType m_PendingRenderPathType = RenderPathType::Forward;

        bool m_SceneLoadPending = false;
        std::string m_PendingScenePath;

        std::shared_ptr<ImGuiLayer> m_ImGuiLayer;

        // Command Pool & Buffers
        // VkCommandPool commandPool; // Managed by Context/Renderer now

        // Sync Objects - Moved to Renderer
        // std::vector<FrameData> m_Frames;
        // std::vector<VkFence> imagesInFlight;

        bool framebufferResized = false;

        // Geometry members moved to Scene
        
        // Layers
        float m_LastFrameTime = 0.0f;
        std::vector<std::shared_ptr<Layer>> m_LayerStack;

        VkExtent2D m_LastWindowExtent = { 0, 0 };
    };

    Application* CreateApplication(int argc, char** argv);
}
