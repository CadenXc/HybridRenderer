#pragma once

#include "pch.h"
#include "Scene/EditorCamera.h"
#include "Renderer/Pipelines/RenderPath.h"
#include <glm/glm.hpp>
#include <imgui.h>

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

	struct FrameContext {
		glm::mat4 View{ 1.0f };
		glm::mat4 Projection{ 1.0f };
		glm::vec3 CameraPosition{ 0.0f };
		float Time{ 0.0f };
		float DeltaTime{ 0.0f };
		glm::vec2 ViewportSize{ 1600.0f, 900.0f };
		uint32_t FrameIndex{ 0 };
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
		void Close();

		void RequestScreenshot(const std::string& filename) { m_ScreenshotFilename = filename; m_ScreenshotRequested = true; }

		std::shared_ptr<VulkanContext> GetContext() { return m_Context; }
		std::shared_ptr<Renderer> GetRenderer() { return m_Renderer; }
		ImGuiLayer* GetImGuiLayer() { return m_ImGuiLayer.get(); }
		RenderPath* GetRenderPath() { return m_RenderPath.get(); }
		PipelineManager& GetPipelineManager() { return *m_PipelineManager; }
		RenderPathType GetCurrentRenderPathType() const;
		Scene* GetScene() { return m_Scene.get(); }
		ResourceManager* GetResourceManager() { return m_ResourceManager.get(); }
		Window& GetWindow() { return *m_Window; }
		const ApplicationSpecification& GetSpecification() const { return m_Specification; }

		// Frame State
		void SetFrameContext(const FrameContext& context) { m_FrameContext = context; }
		const FrameContext& GetFrameContext() const { return m_FrameContext; }

		void RequestShaderReload() { m_ShaderReloadPending = true; }
		void RecompileShaders();

	private:
		void initVulkan();
		void cleanup();

		void drawFrame();

		void ExecuteRenderPathSwitch(RenderPathType type);
		void ExecuteLoadScene(const std::string& path);

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
		std::unique_ptr<PipelineManager> m_PipelineManager;

		std::unique_ptr<RenderPath> m_RenderPath;

		FrameContext m_FrameContext;

		bool m_RenderPathSwitchPending = false;
		RenderPathType m_PendingRenderPathType = RenderPathType::Hybrid;

		bool m_SceneLoadPending = false;
		bool m_ShaderReloadPending = false;
		std::string m_PendingScenePath;

		bool m_ScreenshotRequested = false;
		std::string m_ScreenshotFilename;

		// Layers
		float m_LastFrameTime = 0.0f;
		std::vector<std::shared_ptr<Layer>> m_LayerStack;
		std::unique_ptr<ImGuiLayer> m_ImGuiLayer;
	};

	Application* CreateApplication(int argc, char** argv);
}
