#pragma once

#include "Core/Layer.h"
#include "Core/Application.h"
#include "Scene/EditorCamera.h"
#include "Scene/Scene.h"
#include "Renderer/Pipelines/RenderPath.h"
#include "Renderer/Graph/ResourceNames.h"
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <imgui.h>

namespace Chimera
{

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
		void ClearSkybox();

		std::shared_ptr<Scene> GetActiveScene()
		{
			return ResourceManager::Get().GetActiveSceneShared();
		}
		Scene* GetActiveSceneRaw()
		{
			return ResourceManager::Get().GetActiveScene();
		}
		RenderPath* GetRenderPath()
		{
			return Application::Get().GetActiveRenderPath();
		}

	private:
		void RefreshModelList();
		void RefreshSkyboxList();
		void LoadModel(const std::string& relativePath);

		// UI Panels		 
		void DrawMenuBar();
		void DrawRenderPathPanel();
		void DrawModelSelectionPanel();
		void DrawViewportContent();
		void DrawControlPanelContent();
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
        
		// UI Visibility
		bool m_ShowControlPanel = true;
		bool m_ShowViewport = true;
		uint32_t m_DisplayMode = 0; // 0: Final Output
		uint32_t m_RenderFlags = 3; // Bit 0: SVGF, Bit 1: TAA
				
		float m_Exposure = 1.0f;
        float m_AmbientStrength = 1.0f;
        float m_BloomStrength = 0.5f;

        // --- SVGF Parameters ---
        float m_SVGFAlphaColor = 0.05f;
        float m_SVGFAlphaMoments = 0.2f;
        float m_SVGFPhiColor = 10.0f;
        float m_SVGFPhiNormal = 128.0f;
        float m_SVGFPhiDepth = 0.1f;

        // --- Light Parameters ---
        float m_LightRadius = 0.05f;

        // Model/Scene Management
		struct ModelAsset 
		{
			std::string Name;
			std::string Path;
		};
		std::vector<ModelAsset> m_AvailableModels;
		std::string m_ActiveModelPath = "";
		int m_SelectedModelIndex = -1;

		std::vector<std::string> m_AvailableSkyboxes;
		int m_SelectedSkyboxIndex = 0;

		int m_SelectedInstanceIndex = -1;

		// Performance Metrics
		float m_AverageFrameTime = 0.0f;
		float m_AverageFPS = 0.0f;

		// Resize debounce
		float m_ResizeTimer = 0.0f;
		bool m_ViewportResizing = false;
		bool m_ResizePending = false;
		glm::vec2 m_NextViewportSize{ 0.0f };
	};

}
