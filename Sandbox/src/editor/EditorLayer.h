#pragma once

#include "Core/Layer.h"
#include "Core/Application.h"
#include "Scene/EditorCamera.h"
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
		EditorLayer(Application* app);
		~EditorLayer() = default;

		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnUpdate(Timestep ts) override;
		virtual void OnUIRender() override;
		virtual void OnEvent(Event& e) override;

	private:
		void RefreshModelList();
		void LoadModel(const std::string& relativePath);

		// UI Panels		 
		void DrawMenuBar();
		void DrawStatsPanel();
		void DrawRenderPathPanel();
		void DrawModelSelectionPanel();
		void DrawViewport();
		void DrawSettingsPanel();
		void DrawSceneHierarchy();

	private:
		Application* m_App;
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
	};

}
