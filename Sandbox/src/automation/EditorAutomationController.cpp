#include "pch.h"
#include "EditorAutomationController.h"

#include "Core/Application.h"
#include "Renderer/Backend/Renderer.h"
#include "Renderer/Pipelines/RenderPath.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Scene/EditorCamera.h"
#include "Scene/Scene.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace Chimera
{
namespace
{
std::filesystem::path MakeSmokeOutputDirectory(const char* rootDirectory)
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t timestamp = std::chrono::system_clock::to_time_t(now);
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) %
        1000;
    std::tm localTime{};
    localtime_s(&localTime, &timestamp);

    std::ostringstream directoryName;
    directoryName << "run-" << std::put_time(&localTime, "%Y%m%d-%H%M%S")
                  << '-' << std::setfill('0') << std::setw(3)
                  << milliseconds.count();
    return std::filesystem::current_path() / rootDirectory /
           directoryName.str();
}

bool IsReadyForCapture(Scene* scene, RenderPath* activePath,
                       bool sceneReady, bool requireEntity)
{
    return sceneReady && activePath && activePath->IsReadyForCapture() &&
           !ResourceManager::Get().HasPendingModelLoads() && scene &&
           (!requireEntity || !scene->GetEntities().empty()) &&
           !scene->HasPendingGpuUpdates();
}
} // namespace

EditorAutomationController::EditorAutomationController(
    EditorAutomationOptions options)
    : m_Options(options)
{
}

bool EditorAutomationController::IsActive() const
{
    return m_Options.taaDisocclusionSmokeTest ||
           m_Options.objectMotionSmokeTest;
}

void EditorAutomationController::ConfigureRenderSettings(
    RenderFlags& renderFlags, DisplayMode& displayMode,
    bool& showControlPanel) const
{
    if (m_Options.taaDisocclusionSmokeTest)
    {
        renderFlags |= RenderFlags_TAABit;
        displayMode = DisplayMode::TAAHistory;
        showControlPanel = false;
    }
    else if (m_Options.objectMotionSmokeTest)
    {
        renderFlags = RenderFlags_LightBit;
        displayMode = DisplayMode::Motion;
        showControlPanel = false;
    }
}

void EditorAutomationController::Initialize()
{
    if (m_Options.taaDisocclusionSmokeTest)
    {
        InitializeTaaDisocclusionSmokeTest();
    }
    else if (m_Options.objectMotionSmokeTest)
    {
        InitializeObjectMotionSmokeTest();
    }
}

void EditorAutomationController::UpdateBeforeScene(
    Scene* scene, RenderPath* activePath, bool sceneReady, bool sceneFailed)
{
    UpdateObjectMotionSmokeTest(scene, activePath, sceneReady, sceneFailed);
}

void EditorAutomationController::UpdateAfterScene(
    EditorCamera& camera, Scene* scene, RenderPath* activePath,
    bool sceneReady, bool sceneFailed)
{
    UpdateTaaDisocclusionSmokeTest(camera, scene, activePath, sceneReady,
                                   sceneFailed);
}

void EditorAutomationController::InitializeTaaDisocclusionSmokeTest()
{
    m_TaaSmokeOutputDirectory =
        MakeSmokeOutputDirectory("taa-smoke-results");
    m_TaaSmokeStableCapturePath =
        m_TaaSmokeOutputDirectory / "stable-history.png";
    m_TaaSmokeMovedCapturePath =
        m_TaaSmokeOutputDirectory / "moved-history.png";

    std::error_code directoryError;
    std::filesystem::create_directories(m_TaaSmokeOutputDirectory,
                                        directoryError);
    if (directoryError)
    {
        CH_CORE_ERROR("TAA disocclusion smoke test could not create {}: {}",
                      m_TaaSmokeOutputDirectory.string(),
                      directoryError.message());
        m_TaaSmokeState = TaaDisocclusionSmokeState::Finished;
        Application::Get().Close();
        return;
    }

    m_TaaSmokeState = TaaDisocclusionSmokeState::WaitingForScene;
    m_TaaSmokeStateFrameCount = 0;
    CH_CORE_INFO("TAA disocclusion smoke test started; output: {}",
                 m_TaaSmokeOutputDirectory.string());
}

void EditorAutomationController::InitializeObjectMotionSmokeTest()
{
    m_ObjectMotionSmokeOutputDirectory =
        MakeSmokeOutputDirectory("object-motion-smoke-results");
    m_ObjectMotionBaselineCapturePath =
        m_ObjectMotionSmokeOutputDirectory / "baseline-motion.png";
    m_ObjectMotionMovedCapturePath =
        m_ObjectMotionSmokeOutputDirectory / "moved-motion.png";
    m_ObjectMotionStoppedCapturePath =
        m_ObjectMotionSmokeOutputDirectory / "stopped-motion.png";

    std::error_code directoryError;
    std::filesystem::create_directories(
        m_ObjectMotionSmokeOutputDirectory, directoryError);
    if (directoryError)
    {
        CH_CORE_ERROR("Object motion smoke test could not create {}: {}",
                      m_ObjectMotionSmokeOutputDirectory.string(),
                      directoryError.message());
        m_ObjectMotionSmokeState = ObjectMotionSmokeState::Finished;
        Application::Get().Close();
        return;
    }

    m_ObjectMotionSmokeState = ObjectMotionSmokeState::WaitingForScene;
    m_ObjectMotionSmokeStateFrameCount = 0;
    CH_CORE_INFO("Object motion smoke test started; output: {}",
                 m_ObjectMotionSmokeOutputDirectory.string());
}

void EditorAutomationController::FinishObjectMotionSmokeTest(
    bool passed, const std::string& reason)
{
    const std::filesystem::path resultPath =
        m_ObjectMotionSmokeOutputDirectory / "result.txt";
    std::ofstream resultFile(resultPath);
    if (resultFile)
    {
        resultFile << (passed ? "PASS" : "FAIL") << '\n'
                   << "reason=" << reason << '\n'
                   << "baselineCapture="
                   << m_ObjectMotionBaselineCapturePath.string() << '\n'
                   << "movedCapture="
                   << m_ObjectMotionMovedCapturePath.string() << '\n'
                   << "movedDifferentPixels="
                   << m_ObjectMotionMovedComparison.differentPixelCount
                   << '\n'
                   << "movedMaxChannelDifference="
                   << static_cast<uint32_t>(
                          m_ObjectMotionMovedComparison.maxChannelDifference)
                   << '\n'
                   << "movedRmse=" << std::fixed << std::setprecision(6)
                   << m_ObjectMotionMovedComparison.rmse << '\n'
                   << "stoppedCapture="
                   << m_ObjectMotionStoppedCapturePath.string() << '\n'
                   << "stoppedDifferentPixels="
                   << m_ObjectMotionStoppedComparison.differentPixelCount
                   << '\n'
                   << "stoppedMaxChannelDifference="
                   << static_cast<uint32_t>(
                          m_ObjectMotionStoppedComparison.maxChannelDifference)
                   << '\n'
                   << "stoppedRmse="
                   << m_ObjectMotionStoppedComparison.rmse << '\n';
    }

    if (passed)
    {
        CH_CORE_INFO("Object motion smoke test PASSED: {}", reason);
    }
    else
    {
        CH_CORE_ERROR("Object motion smoke test FAILED: {}", reason);
    }
    CH_CORE_INFO("Object motion smoke test result: {}", resultPath.string());

    m_ObjectMotionSmokeState = ObjectMotionSmokeState::Finished;
    Application::Get().Close();
}

void EditorAutomationController::UpdateObjectMotionSmokeTest(
    Scene* scene, RenderPath* activePath, bool sceneReady, bool sceneFailed)
{
    if (m_ObjectMotionSmokeState == ObjectMotionSmokeState::Disabled ||
        m_ObjectMotionSmokeState == ObjectMotionSmokeState::Finished)
    {
        return;
    }

    ++m_ObjectMotionSmokeStateFrameCount;
    if (m_ObjectMotionSmokeStateFrameCount > 900)
    {
        FinishObjectMotionSmokeTest(
            false, "timed out while waiting for the current test phase");
        return;
    }

    const bool ready =
        IsReadyForCapture(scene, activePath, sceneReady, true);

    switch (m_ObjectMotionSmokeState)
    {
        case ObjectMotionSmokeState::WaitingForScene:
        {
            if (sceneFailed)
            {
                FinishObjectMotionSmokeTest(
                    false, "benchmark scene failed to load");
                return;
            }
            if (!ready)
            {
                return;
            }

            m_ObjectMotionSmokeState = ObjectMotionSmokeState::WarmingUp;
            m_ObjectMotionSmokeWarmupFrameCount = 0;
            m_ObjectMotionSmokeStateFrameCount = 0;
            CH_CORE_INFO("Object motion smoke: scene ready; warming up");
            return;
        }
        case ObjectMotionSmokeState::WarmingUp:
        {
            if (!ready)
            {
                return;
            }

            constexpr uint32_t WarmupFrameCount = 8;
            ++m_ObjectMotionSmokeWarmupFrameCount;
            if (m_ObjectMotionSmokeWarmupFrameCount < WarmupFrameCount)
            {
                return;
            }

            if (!Renderer::Get().RequestFrameCapture(
                    m_ObjectMotionBaselineCapturePath))
            {
                FinishObjectMotionSmokeTest(
                    false, "baseline motion capture request was rejected");
                return;
            }

            m_ObjectMotionSmokeState =
                ObjectMotionSmokeState::WaitingForBaselineCapture;
            m_ObjectMotionSmokeStateFrameCount = 0;
            CH_CORE_INFO("Object motion smoke: baseline capture requested");
            return;
        }
        case ObjectMotionSmokeState::WaitingForBaselineCapture:
        {
            if (!std::filesystem::exists(
                    m_ObjectMotionBaselineCapturePath))
            {
                return;
            }

            const Entity& entity = scene->GetEntities().front();
            scene->UpdateEntityTRS(
                0, entity.transform.position + glm::vec3(0.75f, 0.0f, 0.0f),
                entity.transform.rotation, entity.transform.scale);
            if (!Renderer::Get().RequestFrameCapture(
                    m_ObjectMotionMovedCapturePath))
            {
                FinishObjectMotionSmokeTest(
                    false, "moved motion capture request was rejected");
                return;
            }

            m_ObjectMotionSmokeState =
                ObjectMotionSmokeState::WaitingForMovedCapture;
            m_ObjectMotionSmokeStateFrameCount = 0;
            CH_CORE_INFO("Object motion smoke: object moved; capture requested");
            return;
        }
        case ObjectMotionSmokeState::WaitingForMovedCapture:
        {
            if (!std::filesystem::exists(m_ObjectMotionMovedCapturePath))
            {
                return;
            }

            m_ObjectMotionMovedComparison = ComparePngFiles(
                m_ObjectMotionBaselineCapturePath.string(),
                m_ObjectMotionMovedCapturePath.string(), 2);
            if (!m_ObjectMotionMovedComparison.success)
            {
                FinishObjectMotionSmokeTest(
                    false, m_ObjectMotionMovedComparison.error);
                return;
            }

            if (!Renderer::Get().RequestFrameCapture(
                    m_ObjectMotionStoppedCapturePath))
            {
                FinishObjectMotionSmokeTest(
                    false, "stopped motion capture request was rejected");
                return;
            }

            m_ObjectMotionSmokeState =
                ObjectMotionSmokeState::WaitingForStoppedCapture;
            m_ObjectMotionSmokeStateFrameCount = 0;
            CH_CORE_INFO("Object motion smoke: stopped-frame capture requested");
            return;
        }
        case ObjectMotionSmokeState::WaitingForStoppedCapture:
        {
            if (!std::filesystem::exists(m_ObjectMotionStoppedCapturePath))
            {
                return;
            }

            m_ObjectMotionStoppedComparison = ComparePngFiles(
                m_ObjectMotionBaselineCapturePath.string(),
                m_ObjectMotionStoppedCapturePath.string(), 2);
            if (!m_ObjectMotionStoppedComparison.success)
            {
                FinishObjectMotionSmokeTest(
                    false, m_ObjectMotionStoppedComparison.error);
                return;
            }

            const bool motionAppeared =
                m_ObjectMotionMovedComparison.differentPixelCount >= 64 &&
                m_ObjectMotionMovedComparison.maxChannelDifference >= 8;
            const bool motionStopped =
                m_ObjectMotionStoppedComparison.differentPixelCount == 0;
            if (!motionAppeared || !motionStopped)
            {
                FinishObjectMotionSmokeTest(
                    false,
                    "motion must appear on the moved frame and return to zero on the next frame");
                return;
            }

            FinishObjectMotionSmokeTest(
                true,
                "object motion appeared for one frame and returned to zero");
            return;
        }
        case ObjectMotionSmokeState::Disabled:
        case ObjectMotionSmokeState::Finished:
            return;
    }
}

void EditorAutomationController::FinishTaaDisocclusionSmokeTest(
    bool passed, const std::string& reason)
{
    const uint64_t stableClassified =
        m_TaaSmokeStableStatistics.GetClassifiedPixelCount();
    const uint64_t movedClassified =
        m_TaaSmokeMovedStatistics.GetClassifiedPixelCount();

    const double stableAcceptedRatio =
        stableClassified > 0
            ? static_cast<double>(
                  m_TaaSmokeStableStatistics.acceptedPixelCount) /
                  static_cast<double>(stableClassified)
            : 0.0;
    const double movedRejectedRatio =
        movedClassified > 0
            ? static_cast<double>(
                  m_TaaSmokeMovedStatistics.rejectedPixelCount) /
                  static_cast<double>(movedClassified)
            : 0.0;

    const std::filesystem::path resultPath =
        m_TaaSmokeOutputDirectory / "result.txt";
    std::ofstream resultFile(resultPath);
    if (resultFile)
    {
        resultFile << (passed ? "PASS" : "FAIL") << '\n'
                   << "reason=" << reason << '\n'
                   << "stableCapture="
                   << m_TaaSmokeStableCapturePath.string() << '\n'
                   << "stableAccepted="
                   << m_TaaSmokeStableStatistics.acceptedPixelCount << '\n'
                   << "stableRejected="
                   << m_TaaSmokeStableStatistics.rejectedPixelCount << '\n'
                   << "stableUnclassified="
                   << m_TaaSmokeStableStatistics.unclassifiedPixelCount
                   << '\n'
                   << "stableAcceptedRatio=" << std::fixed
                   << std::setprecision(6) << stableAcceptedRatio << '\n'
                   << "movedCapture="
                   << m_TaaSmokeMovedCapturePath.string() << '\n'
                   << "movedAccepted="
                   << m_TaaSmokeMovedStatistics.acceptedPixelCount << '\n'
                   << "movedRejected="
                   << m_TaaSmokeMovedStatistics.rejectedPixelCount << '\n'
                   << "movedUnclassified="
                   << m_TaaSmokeMovedStatistics.unclassifiedPixelCount << '\n'
                   << "movedRejectedRatio=" << movedRejectedRatio << '\n';
    }

    if (passed)
    {
        CH_CORE_INFO("TAA disocclusion smoke test PASSED: {}", reason);
    }
    else
    {
        CH_CORE_ERROR("TAA disocclusion smoke test FAILED: {}", reason);
    }
    CH_CORE_INFO("TAA disocclusion smoke test result: {}",
                 resultPath.string());

    m_TaaSmokeState = TaaDisocclusionSmokeState::Finished;
    Application::Get().Close();
}

void EditorAutomationController::UpdateTaaDisocclusionSmokeTest(
    EditorCamera& camera, Scene* scene, RenderPath* activePath,
    bool sceneReady, bool sceneFailed)
{
    if (m_TaaSmokeState == TaaDisocclusionSmokeState::Disabled ||
        m_TaaSmokeState == TaaDisocclusionSmokeState::Finished)
    {
        return;
    }

    ++m_TaaSmokeStateFrameCount;
    if (m_TaaSmokeStateFrameCount > 900)
    {
        FinishTaaDisocclusionSmokeTest(
            false, "timed out while waiting for the current test phase");
        return;
    }

    const bool ready =
        IsReadyForCapture(scene, activePath, sceneReady, false);

    switch (m_TaaSmokeState)
    {
        case TaaDisocclusionSmokeState::WaitingForScene:
        {
            if (sceneFailed)
            {
                FinishTaaDisocclusionSmokeTest(
                    false, "benchmark scene failed to load");
                return;
            }
            if (!ready)
            {
                return;
            }

            m_TaaSmokeState = TaaDisocclusionSmokeState::WarmingUp;
            m_TaaSmokeWarmupFrameCount = 0;
            m_TaaSmokeStateFrameCount = 0;
            CH_CORE_INFO("TAA smoke: scene ready; warming temporal history");
            return;
        }
        case TaaDisocclusionSmokeState::WarmingUp:
        {
            if (!ready)
            {
                return;
            }

            constexpr uint32_t WarmupFrameCount = 32;
            ++m_TaaSmokeWarmupFrameCount;
            if (m_TaaSmokeWarmupFrameCount < WarmupFrameCount)
            {
                return;
            }

            if (!Renderer::Get().RequestFrameCapture(
                    m_TaaSmokeStableCapturePath))
            {
                FinishTaaDisocclusionSmokeTest(
                    false, "stable-frame capture request was rejected");
                return;
            }

            m_TaaSmokeState =
                TaaDisocclusionSmokeState::WaitingForStableCapture;
            m_TaaSmokeStateFrameCount = 0;
            CH_CORE_INFO("TAA smoke: stable-frame capture requested");
            return;
        }
        case TaaDisocclusionSmokeState::WaitingForStableCapture:
        {
            if (!std::filesystem::exists(m_TaaSmokeStableCapturePath))
            {
                return;
            }

            m_TaaSmokeStableStatistics = AnalyzeTemporalHistoryPng(
                m_TaaSmokeStableCapturePath.string());
            if (!m_TaaSmokeStableStatistics.success)
            {
                FinishTaaDisocclusionSmokeTest(
                    false, m_TaaSmokeStableStatistics.error);
                return;
            }

            const uint64_t classified =
                m_TaaSmokeStableStatistics.GetClassifiedPixelCount();
            const double acceptedRatio =
                classified > 0
                    ? static_cast<double>(
                          m_TaaSmokeStableStatistics.acceptedPixelCount) /
                          static_cast<double>(classified)
                    : 0.0;
            if (classified == 0 || acceptedRatio < 0.98)
            {
                FinishTaaDisocclusionSmokeTest(
                    false,
                    "stable frame did not accept at least 98% of classified history pixels");
                return;
            }

            // EditorCamera::OnUpdate has already preserved the old view as
            // PrevView. Changing yaw here creates one controlled camera cut.
            camera.SetYaw(camera.GetYaw() + 0.035f);
            if (!Renderer::Get().RequestFrameCapture(
                    m_TaaSmokeMovedCapturePath))
            {
                FinishTaaDisocclusionSmokeTest(
                    false, "moved-frame capture request was rejected");
                return;
            }

            m_TaaSmokeState =
                TaaDisocclusionSmokeState::WaitingForMovedCapture;
            m_TaaSmokeStateFrameCount = 0;
            CH_CORE_INFO("TAA smoke: camera moved; disocclusion capture requested");
            return;
        }
        case TaaDisocclusionSmokeState::WaitingForMovedCapture:
        {
            if (!std::filesystem::exists(m_TaaSmokeMovedCapturePath))
            {
                return;
            }

            m_TaaSmokeMovedStatistics = AnalyzeTemporalHistoryPng(
                m_TaaSmokeMovedCapturePath.string());
            if (!m_TaaSmokeMovedStatistics.success)
            {
                FinishTaaDisocclusionSmokeTest(
                    false, m_TaaSmokeMovedStatistics.error);
                return;
            }

            const uint64_t classified =
                m_TaaSmokeMovedStatistics.GetClassifiedPixelCount();
            const double rejectedRatio =
                classified > 0
                    ? static_cast<double>(
                          m_TaaSmokeMovedStatistics.rejectedPixelCount) /
                          static_cast<double>(classified)
                    : 0.0;
            const uint64_t stableClassified =
                m_TaaSmokeStableStatistics.GetClassifiedPixelCount();
            const double stableRejectedRatio =
                stableClassified > 0
                    ? static_cast<double>(
                          m_TaaSmokeStableStatistics.rejectedPixelCount) /
                          static_cast<double>(stableClassified)
                    : 0.0;
            const bool hasVisibleRejection =
                m_TaaSmokeMovedStatistics.rejectedPixelCount >= 64 &&
                rejectedRatio >= 0.0001;
            const bool exposesAdditionalDisocclusion =
                rejectedRatio >= stableRejectedRatio + 0.001;
            const bool preservesValidHistory =
                m_TaaSmokeMovedStatistics.acceptedPixelCount >
                m_TaaSmokeMovedStatistics.rejectedPixelCount;
            if (!hasVisibleRejection || !exposesAdditionalDisocclusion ||
                !preservesValidHistory)
            {
                FinishTaaDisocclusionSmokeTest(
                    false,
                    "camera motion did not increase rejected history while preserving valid history");
                return;
            }

            FinishTaaDisocclusionSmokeTest(
                true,
                "stable history was accepted and camera motion exposed rejected disocclusions");
            return;
        }
        case TaaDisocclusionSmokeState::Disabled:
        case TaaDisocclusionSmokeState::Finished:
            return;
    }
}

} // namespace Chimera
