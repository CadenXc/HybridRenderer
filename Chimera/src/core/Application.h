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

namespace Chimera
{
    // Forward declarations
    class Window;
    class Layer;
    class ImGuiLayer;
    class VulkanContext;
    class Renderer;
    class RenderState;
    class ResourceManager;
    class PipelineManager;

    struct AppFrameContext
    {
        glm::vec2 ViewportSize;
        glm::mat4 View;
        glm::mat4 Projection;
        glm::vec3 CameraPosition;
        float DeltaTime;
        float Time;
        uint32_t FrameIndex;
    };

    class Application
    {
    public:
        Application(const ApplicationSpecification& spec);
        virtual ~Application();

        static Application& Get()
        {
            return *s_Instance;
        }

        void Run();
        void OnEvent(Event& e);
        
        void PushLayer(std::shared_ptr<Layer> layer);
        void PushOverlay(std::shared_ptr<Layer> layer);

        template<typename T>
        std::shared_ptr<T> GetLayer()
        {
            for (auto& layer : m_LayerStack)
            {
                std::shared_ptr<T> result = std::dynamic_pointer_cast<T>(layer);
                if (result)
                {
                    return result;
                }
            }
            return nullptr;
        }

        // --- System Commands ---
        void Close();

        // --- Getters ---
        Window& GetWindow() { return *m_Window; }
        std::shared_ptr<VulkanContext> GetContext() { return m_Context; }
        Renderer* GetRenderer() { return m_Renderer.get(); }
        RenderState* GetRenderState() { return m_RenderState.get(); }
        std::shared_ptr<ImGuiLayer> GetImGuiLayer() { return m_ImGuiLayer; }

        uint32_t GetCurrentImageIndex() const;
        uint32_t GetTotalFrameCount() const { return m_TotalFrameCount; }

        void SetFrameContext(const AppFrameContext& ctx) { m_FrameContext = ctx; }
        void SetActiveScene(Scene* scene) { m_ActiveScene = scene; }
        float GetDepthScale() const { return m_DepthScale; }
        void SetDepthScale(float scale) { m_DepthScale = scale; }

        void QueueEvent(std::function<void()>&& func)
        {
            std::lock_guard<std::mutex> lock(m_EventQueueMutex);
            m_EventQueue.push_back(std::move(func));
        }

        VkCommandBuffer GetCommandBuffer(bool begin = true);
        void FlushCommandBuffer(VkCommandBuffer cmd);

    private:
        void DrawFrame(Timestep ts); 
        void ProcessEventQueue();
        void UpdateGlobalUBO(uint32_t frameIndex);
        
        bool OnWindowClose(WindowCloseEvent& e);
        bool OnWindowResize(WindowResizeEvent& e);

    private:
        static Application* s_Instance;
        ApplicationSpecification m_Specification;
        
        std::unique_ptr<Window> m_Window;
        std::shared_ptr<VulkanContext> m_Context;
        
        std::unique_ptr<ResourceManager> m_ResourceManager;
        std::unique_ptr<PipelineManager> m_PipelineManager;
        std::unique_ptr<RenderState> m_RenderState;
        std::unique_ptr<Renderer> m_Renderer;
        
        std::shared_ptr<ImGuiLayer> m_ImGuiLayer;
        
        std::vector<std::shared_ptr<Layer>> m_LayerStack;
        unsigned int m_LayerIndex = 0;
        AppFrameContext m_FrameContext;
        Scene* m_ActiveScene = nullptr;
        
        std::deque<std::function<void()>> m_EventQueue;
        std::mutex m_EventQueueMutex;

        bool m_Running = true;
        bool m_Minimized = false;
        float m_LastFrameTime = 0.0f;
        uint32_t m_TotalFrameCount = 0;
        float m_DepthScale = 1.0f;

        glm::mat4 m_PrevView = glm::mat4(1.0f);
        glm::mat4 m_PrevProj = glm::mat4(1.0f);
    };
}