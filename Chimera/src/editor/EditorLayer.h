#pragma once

#include "core/application/Layer.h"
#include "core/application/Application.h"
#include "ImGuiStyle.h"
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

        // UI Panels         
        void DrawMenuBar();
        void DrawStatsPanel();
        void DrawScenePanel();
        void DrawInspectorPanel();
        void DrawResourceBrowserPanel();
        void DrawDebugPanel();
        void DrawRenderPathPanel();
        
        // Utilities
        void DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f, float columnWidth = 100.0f);
        void DrawSeparator();

    private:
        Application* m_App;
        
        // Model/Scene Management
        std::vector<std::string> m_AvailableModels;
        std::string m_CurrentLoadPath = "assets/models";
        int m_SelectedModelIndex = -1;

        // UI Visibility Flags
        bool m_ShowStats = true;
        bool m_ShowScene = true;
        bool m_ShowInspector = true;
        bool m_ShowResourceBrowser = true;
        bool m_ShowDebug = false;
        bool m_ShowRenderPathSettings = false;
        bool m_ShowDemoWindow = false;

        // Performance Metrics
        std::vector<float> m_FrameTimeHistory;
        std::vector<float> m_FpsHistory;
        float m_UpdateTimer = 0.0f;
        float m_AverageFrameTime = 0.0f;

        // ImGui Style
        bool m_StyleChanged = false;
    };

}
