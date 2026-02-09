#pragma once

#include "Core/Layer.h"
#include "Scene/EditorCamera.h"
#include "Scene/Scene.h"
#include "Renderer/Pipelines/RenderPath.h"
#include "Renderer/Graph/ResourceNames.h"
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <imgui.h>

namespace Chimera {

	class EditorLayer : public Layer
	{
	public:
		EditorLayer();
		~EditorLayer() = default;

		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnUpdate(Timestep ts) override;
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Event& e) override;

		void SwitchRenderPath(RenderPathType type);
		void LoadScene(const std::string& path);
		void ClearScene();
		void LoadSkybox(const std::string& path);

		std::shared_ptr<Scene> GetActiveScene() { return m_Scene; }
		RenderPath* GetRenderPath() { return m_RenderPath.get(); }

	private:
		void RefreshModelList();
		void LoadModel(const std::string& relativePath);

		// UI Panels		 
		void DrawMenuBar();
		void DrawStatsPanel();
		void DrawRenderPathPanel();
		void DrawModelSelectionPanel();
		void DrawViewport();
		void DrawSceneHierarchy();
		void DrawPropertiesPanel();
		void DrawLightSettings();

	private:
		EditorCamera m_EditorCamera;
		
		// Viewport & Debugging
		std::string m_DebugViewTexture = RS::FINAL_COLOR;
		VkImageView m_CurrentDebugViewHandle = VK_NULL_HANDLE;
		ImTextureID m_CurrentDebugTexID = 0;
		glm::vec2 m_ViewportSize = { 0, 0 };
		bool m_ViewportHovered = false;
		bool m_ViewportFocused = false;

		// Model/Scene Management
		struct ModelAsset 
		{
			std::string Name;
			std::string Path;
		};
		std::vector<ModelAsset> m_AvailableModels;
		std::string m_ActiveModelPath = "";
		int m_SelectedModelIndex = -1;
		int m_SelectedInstanceIndex = -1;

		// Performance Metrics
		float m_AverageFrameTime = 0.0f;
		float m_AverageFPS = 0.0f;

		// Rendering & Scene (Migrated from Application)
		std::shared_ptr<Scene> m_Scene;
		std::unique_ptr<RenderPath> m_RenderPath;
	};

}
