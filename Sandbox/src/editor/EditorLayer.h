#pragma once

#include "Core/Layer.h"
#include "Core/Application.h"
#include "Scene/EditorCamera.h"
#include "Scene/Scene.h"
#include "Renderer/Pipelines/RenderPath.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Assets/AssetImporter.h"
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
                // UI Panels (Accept active path as parameter)
    void DrawMenuBar();
    void DrawRenderPathPanel(RenderPath* activePath);
    void DrawFeatureToggles(RenderPath* activePath);
    void DrawControlPanelContent(RenderPath* activePath);
    void DrawSceneHierarchy();
    void DrawPropertiesPanel(RenderPath* activePath);
    void DrawLightSettings(RenderPath* activePath);
    void DrawGeneralSettings();

    void RefreshAssetList();
    void ClearScene();

private:
    EditorCamera m_EditorCamera;

                // Interaction & Debugging
    glm::vec2 m_ViewportSize = {0, 0};

                // UI Visibility
    bool m_ShowControlPanel = true;
    DisplayMode m_DisplayMode = DisplayMode::Final;
    RenderFlags m_RenderFlags = RenderFlags_LightBit | RenderFlags_ShadowBit;

    float m_Exposure = 1.0f;
    float m_AmbientStrength = 1.0f;
    glm::vec4 m_ClearColor = {0.1f, 0.1f, 0.1f,
                              1.0f}; // [NEW] Global Background Color

                // --- Light Parameters ---
    float m_LightRadius = 0.05f;

                // Shared Asset Management (Models/HDRs)
    char m_AssetSearchFilter[256] = {0};
    std::string m_ActiveAssetPath = "";
    int m_SelectedAssetIndex = -1;

    std::vector<AssetInfo> m_AvailableModels;
    std::vector<AssetInfo> m_AvailableHDRs;

    int m_SelectedInstanceIndex = -1;

                // Performance Metrics
    float m_AverageFrameTime = 0.0f;
    float m_AverageFPS = 0.0f;

                // Resize debounce
    float m_ResizeTimer = 0.0f;
    bool m_ViewportResizing = false;
    bool m_ResizePending = false;
    glm::vec2 m_NextViewportSize{0.0f};
};

} // namespace Chimera
