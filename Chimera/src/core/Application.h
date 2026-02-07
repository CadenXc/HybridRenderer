#pragma once

#include "pch.h"
#include "Renderer/Backend/Renderer.h"
#include "Renderer/SceneRenderer.h"
#include <glm/glm.hpp>
#include <imgui.h>
#include <queue>
#include <mutex>
#include <functional>

namespace Chimera {

	class VulkanContext;
	class Renderer;
	class Scene;
	class Layer;
	class ImGuiLayer;
	class Buffer;
	class Image;
	class ResourceManager;
	class Window;
	class PipelineManager;
	class SceneRenderer;
    class RenderState;
	class Event;
	class WindowResizeEvent;
	class WindowCloseEvent;

	struct ApplicationSpecification
	{
		std::string Name = "Chimera App";
		uint32_t Width = 1600;
		uint32_t Height = 900;
		bool WindowResizeable = true;
	};

	class Application
	{
	public:
		Application(const ApplicationSpecification& spec = ApplicationSpecification());
		virtual ~Application();

		static Application& Get() { return *s_Instance; }

		void Run();
		void OnEvent(Event& e);
		void PushLayer(const std::shared_ptr<Layer>& layer);
		void SwitchRenderPath(RenderPathType type);
		void LoadScene(const std::string& path);
		void LoadSkybox(const std::string& path);
		void ClearScene();
		void Close();

		void RequestScreenshot(const std::string& filename) { m_ScreenshotFilename = filename; m_ScreenshotRequested = true; }

		std::shared_ptr<VulkanContext> GetContext() { return m_Context; }
		std::shared_ptr<Renderer> GetRenderer() { return m_Renderer; }
		
		RenderPath* GetRenderPath() { return m_RenderPath.get(); }
		PipelineManager& GetPipelineManager() { return *m_PipelineManager; }
		RenderPathType GetCurrentRenderPathType() const;
		uint32_t GetCurrentImageIndex() const { return m_Renderer->GetCurrentImageIndex(); }
		Scene* GetScene() { return m_Scene.get(); }
		ResourceManager* GetResourceManager() { return m_ResourceManager.get(); }
		Window& GetWindow() { return *m_Window; }
		const ApplicationSpecification& GetSpecification() const { return m_Specification; }
		std::shared_ptr<ImGuiLayer> GetImGuiLayer() { return m_ImGuiLayer; }
        RenderState* GetRenderState() { return m_RenderState.get(); }

		// Frame State
		void SetFrameContext(const FrameContext& context) { m_FrameContext = context; }
		const FrameContext& GetFrameContext() const { return m_FrameContext; }
		uint32_t GetTotalFrameCount() const { return m_TotalFrameCount; }

		void RequestShaderReload();
		void RecompileShaders();

        // Static Convenience Accessors (Walnut style)
        static VkDevice GetDevice() { return s_Instance->m_Context->GetDevice(); }
        static VkPhysicalDevice GetPhysicalDevice() { return s_Instance->m_Context->GetPhysicalDevice(); }
        static VmaAllocator GetAllocator() { return s_Instance->m_Context->GetAllocator(); }
        static VkQueue GetGraphicsQueue() { return s_Instance->m_Context->GetGraphicsQueue(); }
        static VulkanContext& GetVulkanContext() { return *s_Instance->m_Context; }

        static VkCommandBuffer GetCommandBuffer(bool begin);
        static void FlushCommandBuffer(VkCommandBuffer commandBuffer);

        // Reference Walnut: Thread-safe event queue for deferred execution
        template<typename Func>
        void QueueEvent(Func&& func)
        {
            std::scoped_lock<std::mutex> lock(m_EventQueueMutex);
            m_EventQueue.push(func);
        }

	protected:
		void ExecuteRenderPathSwitch(RenderPathType type);
		virtual void ExecuteLoadScene(const std::string& path);
		void ExecuteLoadSkybox(const std::string& path);
		void ExecuteClearScene();

	private:
		void initVulkan();
		void cleanup();

		void drawFrame();

		bool OnWindowResize(WindowResizeEvent& e);
		bool OnWindowClose(WindowCloseEvent& e);

	private:
		static Application* s_Instance;
		ApplicationSpecification m_Specification;
		std::unique_ptr<Window> m_Window;
		std::shared_ptr<VulkanContext> m_Context;
		std::shared_ptr<Renderer> m_Renderer;
		std::shared_ptr<Scene> m_Scene;
		std::unique_ptr<ResourceManager> m_ResourceManager;
        std::unique_ptr<RenderState> m_RenderState;
		std::unique_ptr<PipelineManager> m_PipelineManager;
		std::unique_ptr<SceneRenderer> m_SceneRenderer;

		std::unique_ptr<RenderPath> m_RenderPath;

		FrameContext m_FrameContext;

        std::mutex m_EventQueueMutex;
        std::queue<std::function<void()>> m_EventQueue;

		bool m_ScreenshotRequested = false;
		std::string m_ScreenshotFilename;

		uint32_t m_TotalFrameCount = 0;

		// Layers
		float m_LastFrameTime = 0.0f;
		std::vector<std::shared_ptr<Layer>> m_LayerStack;
		std::shared_ptr<ImGuiLayer> m_ImGuiLayer;
	};

	Application* CreateApplication(int argc, char** argv);
}