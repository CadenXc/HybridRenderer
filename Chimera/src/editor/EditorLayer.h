#pragma once

#include "core/application/Layer.h"
#include "core/application/Application.h"
#include <vector>
#include <string>
#include <glm/glm.hpp> //    ?GLM

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

        // UI          
        void DrawMenuBar();
        void DrawStatsPanel();
        void DrawHierarchyPanel();
        void DrawInspectorPanel();
        void DrawResourceBrowserPanel();
        
        //     XYZ       
        void DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f, float columnWidth = 100.0f);

    private:
        Application* m_App;
        
        //           ?
        std::vector<std::string> m_AvailableModels;
        std::string m_CurrentLoadPath = "assets/models";
        int m_SelectedModelIndex = -1;

        // UI    ?
        bool m_ShowStats = true;
        bool m_ShowHierarchy = true;
        bool m_ShowInspector = true;
        bool m_ShowDemoWindow = false;

        //          
        std::vector<float> m_FrameTimeHistory;
        float m_UpdateTimer = 0.0f;
    };

}

