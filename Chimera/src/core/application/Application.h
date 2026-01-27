#pragma once

#include "pch.h"
#include "gfx/vulkan/VulkanContext.h"
#include "gfx/vulkan/Renderer.h"
#include "gfx/core/RenderPath.h"
#include "core/scene/Scene.h"
#include "core/application/Layer.h"
#include "gfx/resources/Buffer.h"
#include "gfx/resources/Image.h"
#include "core/scene/CameraController.h"
#include "gfx/resources/ResourceManager.h"
#include "core/Config.h"
#include <imgui.h>

namespace Chimera {

    class Buffer;
    class Image;
    // Vertex struct moved to Scene.h

    enum class RenderPathType {
        Forward,
        RayTracing,
        Hybrid
    };

    struct ApplicationSpecification
    {
        std::string Name = "Chimera App";
        uint32_t Width = 1600;
        uint32_t Height = 900;
        bool WindowResizeable = true;
    };

    class Application {
    public:
        Application(const ApplicationSpecification& spec = ApplicationSpecification());
        virtual ~Application();

        void Run();
        void PushLayer(const std::shared_ptr<Layer>& layer);
        void SwitchRenderPath(RenderPathType type);
        void LoadScene(const std::string& path);
        void Close();

        // Expose Context and Renderer to Layers (for now, until we abstract further)
        std::shared_ptr<VulkanContext> GetContext() { return m_Context; }
        std::shared_ptr<Renderer> GetRenderer() { return m_Renderer; }
        RenderPath* GetRenderPath() { return m_RenderPath.get(); }
        RenderPathType GetCurrentRenderPathType() const { return m_CurrentRenderPathType; }
        GLFWwindow* GetWindow() { return m_Window; }
        const ApplicationSpecification& GetSpecification() const { return m_Specification; }

        VkRenderPass GetImGuiRenderPass() const { return m_ImGuiRenderPass; }

    private:
        void initWindow();
        void initVulkan();
        void mainLoop();
        void cleanup();

        void cleanupSwapChain();
        
        // ImGui
        void InitImGui();
        void ShutdownImGui();
        void BeginImGuiFrame();
        void EndImGuiFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex);
        void CreateImGuiRenderPass();
        void CreateImGuiFramebuffers();
        void SetDarkThemeColors();

        // Ray Tracing

        void drawFrame();
        void updateUniformBuffer(uint32_t currentImage);
        void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);

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
        ApplicationSpecification m_Specification;
        GLFWwindow* m_Window;
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

        // ImGui Resources
        VkDescriptorPool m_ImGuiDescriptorPool = VK_NULL_HANDLE;
        VkRenderPass m_ImGuiRenderPass = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> m_ImGuiFramebuffers;

        bool m_FramebufferResized = false;
        
        // Layers
        float m_LastFrameTime = 0.0f;
        std::vector<std::shared_ptr<Layer>> m_LayerStack;

        VkExtent2D m_LastWindowExtent = { 0, 0 };
    };

    Application* CreateApplication(int argc, char** argv);
}



