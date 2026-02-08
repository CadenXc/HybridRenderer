#pragma once

#include "pch.h"
#include "Renderer/ChimeraCommon.h"
#include "Core/Events/Event.h"
#include "Core/Events/ApplicationEvent.h"
#include "Core/Window.h"
#include "Core/Layer.h"
#include <memory>
#include <mutex>
#include <vector>
#include <deque>

namespace Chimera {

    struct AppFrameContext {
        glm::vec2 ViewportSize;
        glm::mat4 View;
        glm::mat4 Projection;
        glm::vec3 CameraPosition;
        float DeltaTime;
        float Time;
        uint32_t FrameIndex;
    };

    class Application {
    public:
        Application(const ApplicationSpecification& spec);
        virtual ~Application();

        static Application& Get() { return *s_Instance; }

        void Run();
        void OnEvent(Event& e);
        
        void PushLayer(std::shared_ptr<Layer> layer);
        void PushOverlay(std::shared_ptr<Layer> layer);

        void SwitchRenderPath(RenderPathType type);
        void RecompileShaders();
        void RequestShaderReload();
        void drawFrame();
        void ExecuteRenderPathSwitch(RenderPathType type);
        
        void LoadScene(const std::string& path);
        void ExecuteLoadScene(const std::string& path);
        void ClearScene();
        void ExecuteClearScene();
        void LoadSkybox(const std::string& path);
        void ExecuteLoadSkybox(const std::string& path);
        void Close();

        Window& GetWindow() { return *m_Window; }
        std::shared_ptr<class Scene> GetScene() { return m_Scene; }
        class RenderPath* GetRenderPath() { return m_RenderPath.get(); }
        class Renderer* GetRenderer() { return m_Renderer.get(); }
        class RenderState* GetRenderState() { return m_RenderState.get(); }
        class ResourceManager* GetResourceManager() { return m_ResourceManager.get(); }
        class PipelineManager* GetPipelineManager() { return m_PipelineManager.get(); }
        std::shared_ptr<class ImGuiLayer> GetImGuiLayer() { return m_ImGuiLayer; }
        uint32_t GetCurrentImageIndex() const;
        
        uint32_t GetTotalFrameCount() const { return m_TotalFrameCount; }
        void SetFrameContext(const AppFrameContext& ctx) { m_FrameContext = ctx; }

        void QueueEvent(std::function<void()>&& func) {
            std::lock_guard<std::mutex> lock(m_EventQueueMutex);
            m_EventQueue.push_back(std::move(func));
        }

        VkCommandBuffer GetCommandBuffer(bool begin = true);
        void FlushCommandBuffer(VkCommandBuffer cmd);
        RenderPathType GetCurrentRenderPathType() const;

    private:
        bool OnWindowClose(WindowCloseEvent& e);
        bool OnWindowResize(WindowResizeEvent& e);
        void cleanup();

    private:
        static Application* s_Instance;
        ApplicationSpecification m_Specification;
        
        std::unique_ptr<Window> m_Window;
        std::shared_ptr<class VulkanContext> m_Context;
        std::unique_ptr<class ResourceManager> m_ResourceManager;
        std::unique_ptr<class PipelineManager> m_PipelineManager;
        std::unique_ptr<class RenderState> m_RenderState;
        std::shared_ptr<class Renderer> m_Renderer;
        
        std::shared_ptr<class ImGuiLayer> m_ImGuiLayer;
        std::unique_ptr<class SceneRenderer> m_SceneRenderer;
        std::shared_ptr<class Scene> m_Scene;
        std::unique_ptr<class RenderPath> m_RenderPath;
        
        std::vector<std::shared_ptr<Layer>> m_LayerStack;
        unsigned int m_LayerIndex = 0;
        AppFrameContext m_FrameContext;
        
        std::deque<std::function<void()>> m_EventQueue;
        std::mutex m_EventQueueMutex;

        bool m_Running = true;
        bool m_Minimized = false;
        float m_LastFrameTime = 0.0f;
        uint32_t m_TotalFrameCount = 0;
    };

}