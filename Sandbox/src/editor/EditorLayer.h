#pragma once

#include "core/application/Layer.h"
#include "core/application/Application.h"
#include <vector>
#include <string>
#include <glm/glm.hpp>

namespace Chimera {

	class EditorLayer : public Layer
	{
	public:
		EditorLayer(Application* app);
		~EditorLayer() = default;

		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnUpdate(float ts) override;
		virtual void OnUIRender() override;

	private:
		void RefreshModelList();
		void LoadDefaultModel();

		// UI Panels		 
		void DrawMenuBar();
		void DrawStatsPanel();
		void DrawRenderPathPanel();
		void DrawModelSelectionPanel();
		void DrawShaderSelectionPanel();

	private:
		Application* m_App;
		
		// Model/Scene Management
		std::vector<std::string> m_AvailableModels;
		std::string m_CurrentLoadPath = "assets/models";
		int m_SelectedModelIndex = -1;
		bool m_ModelLoaded = false;

		// Shader Management
		int m_SelectedRenderPathIndex = 0; // 0: Forward, 1: RayTracing, 2: Hybrid
		const char* m_RenderPathNames[3] = { "Forward", "Ray Tracing", "Hybrid" };

		// Performance Metrics
		std::vector<float> m_FrameTimeHistory;
		std::vector<float> m_FpsHistory;
		float m_UpdateTimer = 0.0f;
		float m_AverageFrameTime = 0.0f;
	};

}
