#pragma once

#include "Renderer/Backend/ShaderCommon.h"
#include "Renderer/Capture/ImageComparison.h"
#include "Renderer/Capture/TemporalHistoryAnalysis.h"

#include <filesystem>
#include <string>

namespace Chimera
{

class EditorCamera;
class RenderPath;
class Scene;

struct EditorAutomationOptions
{
    bool taaDisocclusionSmokeTest = false;
    bool objectMotionSmokeTest = false;
};

class EditorAutomationController
{
public:
    explicit EditorAutomationController(
        EditorAutomationOptions options = {});

    bool IsActive() const;
    void ConfigureRenderSettings(RenderFlags& renderFlags,
                                 DisplayMode& displayMode,
                                 bool& showControlPanel) const;
    void Initialize();

    // Object motion must be applied before Scene::OnUpdate uploads the frame.
    void UpdateBeforeScene(Scene* scene, RenderPath* activePath,
                           bool sceneReady, bool sceneFailed);

    // Camera cuts must be applied after EditorCamera::OnUpdate preserves the
    // previous matrices used to calculate this frame's motion vectors.
    void UpdateAfterScene(EditorCamera& camera, Scene* scene,
                          RenderPath* activePath, bool sceneReady,
                          bool sceneFailed);

private:
    enum class TaaDisocclusionSmokeState
    {
        Disabled,
        WaitingForScene,
        WarmingUp,
        WaitingForStableCapture,
        WaitingForMovedCapture,
        Finished
    };

    enum class ObjectMotionSmokeState
    {
        Disabled,
        WaitingForScene,
        WarmingUp,
        WaitingForBaselineCapture,
        WaitingForMovedCapture,
        WaitingForStoppedCapture,
        Finished
    };

    void InitializeTaaDisocclusionSmokeTest();
    void UpdateTaaDisocclusionSmokeTest(EditorCamera& camera, Scene* scene,
                                        RenderPath* activePath,
                                        bool sceneReady, bool sceneFailed);
    void FinishTaaDisocclusionSmokeTest(bool passed,
                                        const std::string& reason);
    void InitializeObjectMotionSmokeTest();
    void UpdateObjectMotionSmokeTest(Scene* scene, RenderPath* activePath,
                                     bool sceneReady, bool sceneFailed);
    void FinishObjectMotionSmokeTest(bool passed,
                                     const std::string& reason);

private:
    EditorAutomationOptions m_Options;

    TaaDisocclusionSmokeState m_TaaSmokeState =
        TaaDisocclusionSmokeState::Disabled;
    std::filesystem::path m_TaaSmokeOutputDirectory;
    std::filesystem::path m_TaaSmokeStableCapturePath;
    std::filesystem::path m_TaaSmokeMovedCapturePath;
    TemporalHistoryDebugStatistics m_TaaSmokeStableStatistics;
    TemporalHistoryDebugStatistics m_TaaSmokeMovedStatistics;
    uint32_t m_TaaSmokeWarmupFrameCount = 0;
    uint32_t m_TaaSmokeStateFrameCount = 0;

    ObjectMotionSmokeState m_ObjectMotionSmokeState =
        ObjectMotionSmokeState::Disabled;
    std::filesystem::path m_ObjectMotionSmokeOutputDirectory;
    std::filesystem::path m_ObjectMotionBaselineCapturePath;
    std::filesystem::path m_ObjectMotionMovedCapturePath;
    std::filesystem::path m_ObjectMotionStoppedCapturePath;
    ImageComparisonResult m_ObjectMotionMovedComparison;
    ImageComparisonResult m_ObjectMotionStoppedComparison;
    uint32_t m_ObjectMotionSmokeWarmupFrameCount = 0;
    uint32_t m_ObjectMotionSmokeStateFrameCount = 0;
};

} // namespace Chimera
