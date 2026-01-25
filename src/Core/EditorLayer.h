#pragma once

#include "Layer.h"
#include "Application.h"
#include <vector>
#include <string>
#include <glm/glm.hpp> // 需要 GLM

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

        // UI 绘制辅助函数
        void DrawMenuBar();
        void DrawStatsPanel();
        void DrawHierarchyPanel();
        void DrawInspectorPanel();
        void DrawResourceBrowserPanel();
        
        // 绘制 XYZ 彩色控件
        void DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f, float columnWidth = 100.0f);

    private:
        Application* m_App;
        
        // 资源浏览器状态
        std::vector<std::string> m_AvailableModels;
        std::string m_CurrentLoadPath = "assets/models";
        int m_SelectedModelIndex = -1;

        // UI 开关
        bool m_ShowStats = true;
        bool m_ShowHierarchy = true;
        bool m_ShowInspector = true;
        bool m_ShowDemoWindow = false;

        // 性能图表缓存
        std::vector<float> m_FrameTimeHistory;
        float m_UpdateTimer = 0.0f;
    };

}