#include "pch.h"
#include "EditorAutomationController.h"

#include "Core/Application.h"
#include "Renderer/Backend/Renderer.h"
#include "Renderer/Pipelines/RenderPath.h"
#include "Renderer/Pipelines/RenderSettingsChange.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Scene/EditorCamera.h"
#include "Scene/Scene.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <GLFW/glfw3.h>
#include <stb_image.h>

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

bool HasVisibleScenePixels(const std::filesystem::path& capturePath)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(capturePath.string().c_str(), &width,
                                      &height, &channels, 4);
    if (!pixels || width <= 0 || height <= 0)
    {
        stbi_image_free(pixels);
        return false;
    }

    const int background[3] = {pixels[0], pixels[1], pixels[2]};
    size_t visiblePixelCount = 0;
    const size_t pixelCount = static_cast<size_t>(width) * height;
    for (size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex)
    {
        const unsigned char* pixel = pixels + pixelIndex * 4;
        if (std::abs(static_cast<int>(pixel[0]) - background[0]) >= 8 ||
            std::abs(static_cast<int>(pixel[1]) - background[1]) >= 8 ||
            std::abs(static_cast<int>(pixel[2]) - background[2]) >= 8)
        {
            if (++visiblePixelCount >= 64) break;
        }
    }
    stbi_image_free(pixels);
    return visiblePixelCount >= 64;
}

const char* SvgfSmokeModeName(SvgfSmokeMode mode)
{
    switch (mode)
    {
        case SvgfSmokeMode::SpatialOnly: return "spatial-only";
        case SvgfSmokeMode::TemporalOnly: return "temporal-only";
        case SvgfSmokeMode::TemporalAndSpatial: return "temporal-and-spatial";
        case SvgfSmokeMode::None: return "none";
    }
    return "unknown";
}

RenderFlags WithSvgfSmokeMode(RenderFlags renderFlags, SvgfSmokeMode mode)
{
    if (mode == SvgfSmokeMode::None)
        return renderFlags;

    renderFlags |= RenderFlags_SVGFBit | RenderFlags_GIBit |
                   RenderFlags_ReflectionBit;
    renderFlags &= ~(RenderFlags_SVGFTemporalBit |
                     RenderFlags_SVGFSpatialBit);
    if (mode == SvgfSmokeMode::TemporalOnly ||
        mode == SvgfSmokeMode::TemporalAndSpatial)
        renderFlags |= RenderFlags_SVGFTemporalBit;
    if (mode == SvgfSmokeMode::SpatialOnly ||
        mode == SvgfSmokeMode::TemporalAndSpatial)
        renderFlags |= RenderFlags_SVGFSpatialBit;
    return renderFlags;
}

bool GraphMatchesSvgfSmokeMode(RenderGraph& graph, SvgfSmokeMode mode)
{
    if (mode == SvgfSmokeMode::None)
        return true;

    const auto matchesPresence = [&](const char* suffix, bool expected)
    {
        bool all = true;
        bool any = false;
        for (const char* prefix : {"ShadowAO", "Refl", "GI"})
        {
            const bool present =
                graph.ContainsImage(std::string(prefix) + suffix);
            all &= present;
            any |= present;
        }
        return expected ? all : !any;
    };
    const bool expectTemporal =
        mode == SvgfSmokeMode::TemporalOnly ||
        mode == SvgfSmokeMode::TemporalAndSpatial;
    const bool expectSpatial =
        mode == SvgfSmokeMode::SpatialOnly ||
        mode == SvgfSmokeMode::TemporalAndSpatial;
    return matchesPresence("_Filtered_Final", true) &&
           matchesPresence("_TemporalColor", expectTemporal) &&
           matchesPresence("_Filtered_0", expectSpatial);
}

bool HasCompleteSvgfHistory(const RenderPath& path)
{
    for (const char* historyName : {"ShadowAOAccum", "ShadowAOMoments",
                                    "ReflAccum", "ReflMoments", "GIAccum",
                                    "GIMoments"})
    {
        if (!path.HasUsableHistory(historyName))
            return false;
    }
    return true;
}

bool RejectsPriorHistory(const TemporalHistoryDebugStatistics& stats,
                         uint32_t width, uint32_t height)
{
    const uint64_t totalPixels =
        static_cast<uint64_t>(stats.width) * stats.height;
    return stats.success && stats.width == width && stats.height == height &&
           totalPixels > 0 &&
           stats.rejectedPixelCount >= totalPixels * 99 / 100 &&
           stats.acceptedPixelCount == 0;
}

bool HasRecoveredHistory(const TemporalHistoryDebugStatistics& stats,
                         uint32_t width, uint32_t height)
{
    const uint64_t classified = stats.GetClassifiedPixelCount();
    const uint64_t totalPixels =
        static_cast<uint64_t>(stats.width) * stats.height;
    return stats.success && stats.width == width && stats.height == height &&
           totalPixels > 0 && classified >= totalPixels * 99 / 100 &&
           static_cast<double>(stats.acceptedPixelCount) /
                   static_cast<double>(classified) >= 0.98;
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
           m_Options.objectMotionSmokeTest ||
           m_Options.renderPathSmokeTest;
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
    else if (m_Options.renderPathSmokeTest)
    {
        if (m_Options.svgfSmokeMode != SvgfSmokeMode::None)
            renderFlags = WithSvgfSmokeMode(renderFlags,
                                            m_Options.svgfSmokeMode);
        displayMode = DisplayMode::Final;
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
    else if (m_Options.renderPathSmokeTest)
    {
        InitializeRenderPathSmokeTest();
    }
}

void EditorAutomationController::UpdateBeforeScene(
    Scene* scene, RenderPath* activePath, bool sceneReady, bool sceneFailed)
{
    UpdateObjectMotionSmokeTest(scene, activePath, sceneReady, sceneFailed);
}

void EditorAutomationController::UpdateAfterScene(
    EditorCamera& camera, Scene* scene, RenderPath* activePath,
    bool sceneReady, bool sceneFailed, RenderFlags& renderFlags)
{
    UpdateTaaDisocclusionSmokeTest(camera, scene, activePath, sceneReady,
                                   sceneFailed);
    UpdateRenderPathSmokeTest(scene, activePath, sceneReady, sceneFailed,
                              renderFlags);
}

void EditorAutomationController::InitializeTaaDisocclusionSmokeTest()
{
    m_TaaSmokeOutputDirectory =
        MakeSmokeOutputDirectory("taa-smoke-results");
    m_TaaSmokeStableCapturePath =
        m_TaaSmokeOutputDirectory / "stable-history.png";
    m_TaaSmokeMovedCapturePath =
        m_TaaSmokeOutputDirectory / "moved-history.png";
    m_TaaSmokeResizedCapturePath =
        m_TaaSmokeOutputDirectory / "resized-first-frame.png";
    m_TaaSmokeRecoveredCapturePath =
        m_TaaSmokeOutputDirectory / "resized-recovered.png";
    m_TaaSmokePathFirstCapturePath =
        m_TaaSmokeOutputDirectory / "forward-first-frame.png";
    m_TaaSmokePathRecoveredCapturePath =
        m_TaaSmokeOutputDirectory / "forward-recovered.png";
    m_TaaSmokeSceneFirstCapturePath =
        m_TaaSmokeOutputDirectory / "scene-first-frame.png";
    m_TaaSmokeSceneRecoveredCapturePath =
        m_TaaSmokeOutputDirectory / "scene-recovered.png";
    m_TaaSmokeCameraCutCapturePath =
        m_TaaSmokeOutputDirectory / "camera-cut-first-frame.png";
    m_TaaSmokeCameraRecoveredCapturePath =
        m_TaaSmokeOutputDirectory / "camera-cut-recovered.png";

    std::error_code directoryError;
    std::filesystem::create_directories(m_TaaSmokeOutputDirectory,
                                        directoryError);
    if (directoryError)
    {
        CH_CORE_ERROR("TAA disocclusion smoke test could not create {}: {}",
                      m_TaaSmokeOutputDirectory.string(),
                      directoryError.message());
        m_TaaSmokeState = TaaDisocclusionSmokeState::Finished;
        Application::Get().Close(1);
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
        Application::Get().Close(1);
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
                   << "movedMotionPixels="
                   << m_ObjectMotionMovedStatistics.GetMotionPixelCount()
                   << '\n'
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
                   << m_ObjectMotionStoppedComparison.rmse << '\n'
                   << "stoppedMotionPixels="
                   << m_ObjectMotionStoppedStatistics.GetMotionPixelCount()
                   << '\n';
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
    Application::Get().Close(passed ? 0 : 1);
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

            m_ObjectMotionMovedStatistics = AnalyzeMotionDebugPng(
                m_ObjectMotionMovedCapturePath.string());
            if (!m_ObjectMotionMovedStatistics.success)
            {
                FinishObjectMotionSmokeTest(
                    false, m_ObjectMotionMovedStatistics.error);
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

            m_ObjectMotionStoppedStatistics = AnalyzeMotionDebugPng(
                m_ObjectMotionStoppedCapturePath.string());
            if (!m_ObjectMotionStoppedStatistics.success)
            {
                FinishObjectMotionSmokeTest(
                    false, m_ObjectMotionStoppedStatistics.error);
                return;
            }

            const bool motionAppeared =
                m_ObjectMotionMovedStatistics.GetMotionPixelCount() >= 64;
            const bool motionStopped =
                m_ObjectMotionStoppedStatistics.GetMotionPixelCount() == 0;
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
        if (m_TaaSmokeResizedStatistics.success)
        {
            resultFile << "resizedCapture="
                       << m_TaaSmokeResizedCapturePath.string() << '\n'
                       << "resizedWidth=" << m_TaaSmokeResizedStatistics.width
                       << '\n'
                       << "resizedHeight=" << m_TaaSmokeResizedStatistics.height
                       << '\n'
                       << "resizedAccepted="
                       << m_TaaSmokeResizedStatistics.acceptedPixelCount
                       << '\n'
                       << "resizedRejected="
                       << m_TaaSmokeResizedStatistics.rejectedPixelCount << '\n';
        }
        if (m_TaaSmokeRecoveredStatistics.success)
        {
            resultFile << "recoveredCapture="
                       << m_TaaSmokeRecoveredCapturePath.string() << '\n'
                       << "recoveredAccepted="
                       << m_TaaSmokeRecoveredStatistics.acceptedPixelCount
                       << '\n'
                       << "recoveredRejected="
                       << m_TaaSmokeRecoveredStatistics.rejectedPixelCount
                       << '\n';
        }
        if (m_TaaSmokePathFirstStatistics.success)
        {
            resultFile << "pathFirstCapture="
                       << m_TaaSmokePathFirstCapturePath.string() << '\n'
                       << "pathFirstAccepted="
                       << m_TaaSmokePathFirstStatistics.acceptedPixelCount
                       << '\n'
                       << "pathFirstRejected="
                       << m_TaaSmokePathFirstStatistics.rejectedPixelCount
                       << '\n';
        }
        if (m_TaaSmokePathRecoveredStatistics.success)
        {
            resultFile << "pathRecoveredCapture="
                       << m_TaaSmokePathRecoveredCapturePath.string() << '\n'
                       << "pathRecoveredAccepted="
                       << m_TaaSmokePathRecoveredStatistics.acceptedPixelCount
                       << '\n'
                       << "pathRecoveredRejected="
                       << m_TaaSmokePathRecoveredStatistics.rejectedPixelCount
                       << '\n';
        }
        if (m_TaaSmokeSceneFirstStatistics.success)
        {
            resultFile << "sceneFirstCapture="
                       << m_TaaSmokeSceneFirstCapturePath.string() << '\n'
                       << "sceneFirstAccepted="
                       << m_TaaSmokeSceneFirstStatistics.acceptedPixelCount
                       << '\n'
                       << "sceneFirstRejected="
                       << m_TaaSmokeSceneFirstStatistics.rejectedPixelCount
                       << '\n';
        }
        if (m_TaaSmokeSceneRecoveredStatistics.success)
        {
            resultFile << "sceneRecoveredCapture="
                       << m_TaaSmokeSceneRecoveredCapturePath.string() << '\n'
                       << "sceneRecoveredAccepted="
                       << m_TaaSmokeSceneRecoveredStatistics.acceptedPixelCount
                       << '\n'
                       << "sceneRecoveredRejected="
                       << m_TaaSmokeSceneRecoveredStatistics.rejectedPixelCount
                       << '\n';
        }
        if (m_TaaSmokeCameraCutStatistics.success)
        {
            resultFile << "cameraCutCapture="
                       << m_TaaSmokeCameraCutCapturePath.string() << '\n'
                       << "cameraCutDistance="
                       << m_TaaSmokeCameraCutDistance << '\n'
                       << "cameraCutAccepted="
                       << m_TaaSmokeCameraCutStatistics.acceptedPixelCount
                       << '\n'
                       << "cameraCutRejected="
                       << m_TaaSmokeCameraCutStatistics.rejectedPixelCount
                       << '\n';
        }
        if (m_TaaSmokeCameraRecoveredStatistics.success)
        {
            resultFile << "cameraRecoveredCapture="
                       << m_TaaSmokeCameraRecoveredCapturePath.string() << '\n'
                       << "cameraRecoveredAccepted="
                       << m_TaaSmokeCameraRecoveredStatistics.acceptedPixelCount
                       << '\n'
                       << "cameraRecoveredRejected="
                       << m_TaaSmokeCameraRecoveredStatistics.rejectedPixelCount
                       << '\n';
        }
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
    Application::Get().Close(passed ? 0 : 1);
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

            const VkExtent2D extent =
                Application::Get().GetContext()->GetSwapChainExtent();
            m_TaaSmokeOriginalWidth = extent.width;
            m_TaaSmokeOriginalHeight = extent.height;
            glfwSetWindowSize(
                Application::Get().GetWindow().GetNativeWindow(), 1280, 720);
            m_TaaSmokeState = TaaDisocclusionSmokeState::WaitingForResize;
            m_TaaSmokeStateFrameCount = 0;
            CH_CORE_INFO("TAA smoke: requested window resize");
            return;
        }
        case TaaDisocclusionSmokeState::WaitingForResize:
        {
            int framebufferWidth = 0;
            int framebufferHeight = 0;
            glfwGetFramebufferSize(
                Application::Get().GetWindow().GetNativeWindow(),
                &framebufferWidth, &framebufferHeight);
            if (framebufferWidth <= 0 || framebufferHeight <= 0)
                return;

            const VkExtent2D extent =
                Application::Get().GetContext()->GetSwapChainExtent();
            if (extent.width == m_TaaSmokeOriginalWidth &&
                extent.height == m_TaaSmokeOriginalHeight)
                return;
            if (extent.width != static_cast<uint32_t>(framebufferWidth) ||
                extent.height != static_cast<uint32_t>(framebufferHeight))
                return;
            if (!activePath)
            {
                FinishTaaDisocclusionSmokeTest(
                    false, "render path disappeared during resize");
                return;
            }
            if (activePath->HasUsableHistory("TAAOutput"))
            {
                FinishTaaDisocclusionSmokeTest(
                    false, "old TAA history remained usable after resize");
                return;
            }
            if (!Renderer::Get().RequestFrameCapture(
                    m_TaaSmokeResizedCapturePath))
            {
                FinishTaaDisocclusionSmokeTest(
                    false, "resized first-frame capture request was rejected");
                return;
            }
            m_TaaSmokeState =
                TaaDisocclusionSmokeState::WaitingForResizedCapture;
            m_TaaSmokeStateFrameCount = 0;
            CH_CORE_INFO("TAA smoke: capturing first {}x{} frame after resize",
                         extent.width, extent.height);
            return;
        }
        case TaaDisocclusionSmokeState::WaitingForResizedCapture:
        {
            if (!std::filesystem::exists(m_TaaSmokeResizedCapturePath))
                return;

            m_TaaSmokeResizedStatistics = AnalyzeTemporalHistoryPng(
                m_TaaSmokeResizedCapturePath.string());
            if (!m_TaaSmokeResizedStatistics.success)
            {
                FinishTaaDisocclusionSmokeTest(
                    false, m_TaaSmokeResizedStatistics.error);
                return;
            }
            const VkExtent2D extent =
                Application::Get().GetContext()->GetSwapChainExtent();
            if (!RejectsPriorHistory(m_TaaSmokeResizedStatistics,
                                     extent.width, extent.height))
            {
                FinishTaaDisocclusionSmokeTest(
                    false,
                    "first resized frame did not reject nearly all history pixels");
                return;
            }

            m_TaaSmokeWarmupFrameCount = 0;
            m_TaaSmokeState =
                TaaDisocclusionSmokeState::WarmingUpAfterResize;
            m_TaaSmokeStateFrameCount = 0;
            CH_CORE_INFO("TAA smoke: resized frame rejected history; warming up");
            return;
        }
        case TaaDisocclusionSmokeState::WarmingUpAfterResize:
        {
            if (!ready)
                return;

            if (++m_TaaSmokeWarmupFrameCount < 32)
                return;
            if (!Renderer::Get().RequestFrameCapture(
                    m_TaaSmokeRecoveredCapturePath))
            {
                FinishTaaDisocclusionSmokeTest(
                    false, "recovered-frame capture request was rejected");
                return;
            }
            m_TaaSmokeState =
                TaaDisocclusionSmokeState::WaitingForRecoveredCapture;
            m_TaaSmokeStateFrameCount = 0;
            return;
        }
        case TaaDisocclusionSmokeState::WaitingForRecoveredCapture:
        {
            if (!std::filesystem::exists(m_TaaSmokeRecoveredCapturePath))
                return;

            m_TaaSmokeRecoveredStatistics = AnalyzeTemporalHistoryPng(
                m_TaaSmokeRecoveredCapturePath.string());
            if (!m_TaaSmokeRecoveredStatistics.success)
            {
                FinishTaaDisocclusionSmokeTest(
                    false, m_TaaSmokeRecoveredStatistics.error);
                return;
            }
            if (!HasRecoveredHistory(m_TaaSmokeRecoveredStatistics,
                                     m_TaaSmokeResizedStatistics.width,
                                     m_TaaSmokeResizedStatistics.height))
            {
                FinishTaaDisocclusionSmokeTest(
                    false,
                    "TAA history did not recover after resize warm-up");
                return;
            }
            Application::Get().SwitchRenderPath(RenderPathType::Forward);
            m_TaaSmokeState = TaaDisocclusionSmokeState::WaitingForPathSwitch;
            m_TaaSmokeStateFrameCount = 0;
            CH_CORE_INFO("TAA smoke: requested Hybrid-to-Forward path switch");
            return;
        }
        case TaaDisocclusionSmokeState::WaitingForPathSwitch:
        {
            if (!activePath || activePath->GetType() != RenderPathType::Forward)
                return;
            if (activePath->HasRenderGraph())
            {
                FinishTaaDisocclusionSmokeTest(
                    false, "missed the new render path's first frame");
                return;
            }
            if (activePath->HasUsableHistory("TAAOutput"))
            {
                FinishTaaDisocclusionSmokeTest(
                    false, "new render path inherited TAA history");
                return;
            }
            if (!Renderer::Get().RequestFrameCapture(
                    m_TaaSmokePathFirstCapturePath))
            {
                FinishTaaDisocclusionSmokeTest(
                    false, "first Forward frame capture request was rejected");
                return;
            }
            m_TaaSmokeState =
                TaaDisocclusionSmokeState::WaitingForPathCapture;
            m_TaaSmokeStateFrameCount = 0;
            CH_CORE_INFO("TAA smoke: capturing first Forward frame");
            return;
        }
        case TaaDisocclusionSmokeState::WaitingForPathCapture:
        {
            if (!std::filesystem::exists(m_TaaSmokePathFirstCapturePath))
                return;

            m_TaaSmokePathFirstStatistics = AnalyzeTemporalHistoryPng(
                m_TaaSmokePathFirstCapturePath.string());
            if (!m_TaaSmokePathFirstStatistics.success)
            {
                FinishTaaDisocclusionSmokeTest(
                    false, m_TaaSmokePathFirstStatistics.error);
                return;
            }
            const VkExtent2D extent =
                Application::Get().GetContext()->GetSwapChainExtent();
            if (!RejectsPriorHistory(m_TaaSmokePathFirstStatistics,
                                     extent.width, extent.height))
            {
                FinishTaaDisocclusionSmokeTest(
                    false, "first Forward frame did not reject prior history");
                return;
            }

            m_TaaSmokeWarmupFrameCount = 0;
            m_TaaSmokeState =
                TaaDisocclusionSmokeState::WarmingUpAfterPathSwitch;
            m_TaaSmokeStateFrameCount = 0;
            CH_CORE_INFO("TAA smoke: Forward rejected old history; warming up");
            return;
        }
        case TaaDisocclusionSmokeState::WarmingUpAfterPathSwitch:
        {
            if (!ready || !activePath ||
                activePath->GetType() != RenderPathType::Forward)
                return;

            if (++m_TaaSmokeWarmupFrameCount < 32)
                return;
            if (!Renderer::Get().RequestFrameCapture(
                    m_TaaSmokePathRecoveredCapturePath))
            {
                FinishTaaDisocclusionSmokeTest(
                    false, "Forward recovery capture request was rejected");
                return;
            }
            m_TaaSmokeState =
                TaaDisocclusionSmokeState::WaitingForPathRecoveredCapture;
            m_TaaSmokeStateFrameCount = 0;
            return;
        }
        case TaaDisocclusionSmokeState::WaitingForPathRecoveredCapture:
        {
            if (!std::filesystem::exists(m_TaaSmokePathRecoveredCapturePath))
                return;

            m_TaaSmokePathRecoveredStatistics = AnalyzeTemporalHistoryPng(
                m_TaaSmokePathRecoveredCapturePath.string());
            if (!m_TaaSmokePathRecoveredStatistics.success)
            {
                FinishTaaDisocclusionSmokeTest(
                    false, m_TaaSmokePathRecoveredStatistics.error);
                return;
            }
            if (!HasRecoveredHistory(m_TaaSmokePathRecoveredStatistics,
                                     m_TaaSmokePathFirstStatistics.width,
                                     m_TaaSmokePathFirstStatistics.height))
            {
                FinishTaaDisocclusionSmokeTest(
                    false, "Forward TAA history did not recover after path switch");
                return;
            }
            ResourceManager::Get().ClearScene();
            ResourceManager::Get().LoadScene(
                Application::Get().GetSpecification().AssetDir +
                "models/smoke_test/Box.gltf");
            m_TaaSmokeState = TaaDisocclusionSmokeState::WaitingForSceneReset;
            m_TaaSmokeStateFrameCount = 0;
            CH_CORE_INFO("TAA smoke: requested scene replacement and Box reload");
            return;
        }
        case TaaDisocclusionSmokeState::WaitingForSceneReset:
        {
            if (!scene || !activePath ||
                activePath->GetType() != RenderPathType::Forward)
                return;
            if (activePath->HasUsableHistory("TAAOutput"))
            {
                FinishTaaDisocclusionSmokeTest(
                    false, "old TAA history remained usable after scene replacement");
                return;
            }
            if (!Renderer::Get().RequestFrameCapture(
                    m_TaaSmokeSceneFirstCapturePath))
            {
                FinishTaaDisocclusionSmokeTest(
                    false, "first replacement-scene capture request was rejected");
                return;
            }
            m_TaaSmokeState =
                TaaDisocclusionSmokeState::WaitingForSceneCapture;
            m_TaaSmokeStateFrameCount = 0;
            CH_CORE_INFO("TAA smoke: capturing first frame after scene replacement");
            return;
        }
        case TaaDisocclusionSmokeState::WaitingForSceneCapture:
        {
            if (!std::filesystem::exists(m_TaaSmokeSceneFirstCapturePath))
                return;

            m_TaaSmokeSceneFirstStatistics = AnalyzeTemporalHistoryPng(
                m_TaaSmokeSceneFirstCapturePath.string());
            if (!m_TaaSmokeSceneFirstStatistics.success)
            {
                FinishTaaDisocclusionSmokeTest(
                    false, m_TaaSmokeSceneFirstStatistics.error);
                return;
            }
            const VkExtent2D extent =
                Application::Get().GetContext()->GetSwapChainExtent();
            if (!RejectsPriorHistory(m_TaaSmokeSceneFirstStatistics,
                                     extent.width, extent.height))
            {
                FinishTaaDisocclusionSmokeTest(
                    false, "first replacement-scene frame did not reject prior history");
                return;
            }

            m_TaaSmokeWarmupFrameCount = 0;
            m_TaaSmokeState =
                TaaDisocclusionSmokeState::WarmingUpAfterSceneReset;
            m_TaaSmokeStateFrameCount = 0;
            CH_CORE_INFO("TAA smoke: scene replacement rejected history; waiting for Box");
            return;
        }
        case TaaDisocclusionSmokeState::WarmingUpAfterSceneReset:
        {
            if (!IsReadyForCapture(scene, activePath, sceneReady, true) ||
                activePath->GetType() != RenderPathType::Forward)
                return;

            if (++m_TaaSmokeWarmupFrameCount < 32)
                return;
            if (!Renderer::Get().RequestFrameCapture(
                    m_TaaSmokeSceneRecoveredCapturePath))
            {
                FinishTaaDisocclusionSmokeTest(
                    false, "scene-recovery capture request was rejected");
                return;
            }
            m_TaaSmokeState =
                TaaDisocclusionSmokeState::WaitingForSceneRecoveredCapture;
            m_TaaSmokeStateFrameCount = 0;
            return;
        }
        case TaaDisocclusionSmokeState::WaitingForSceneRecoveredCapture:
        {
            if (!std::filesystem::exists(m_TaaSmokeSceneRecoveredCapturePath))
                return;

            m_TaaSmokeSceneRecoveredStatistics = AnalyzeTemporalHistoryPng(
                m_TaaSmokeSceneRecoveredCapturePath.string());
            if (!m_TaaSmokeSceneRecoveredStatistics.success)
            {
                FinishTaaDisocclusionSmokeTest(
                    false, m_TaaSmokeSceneRecoveredStatistics.error);
                return;
            }
            if (!HasRecoveredHistory(m_TaaSmokeSceneRecoveredStatistics,
                                     m_TaaSmokeSceneFirstStatistics.width,
                                     m_TaaSmokeSceneFirstStatistics.height))
            {
                FinishTaaDisocclusionSmokeTest(
                    false, "TAA history did not recover after scene replacement");
                return;
            }
            ChimeraAABB sceneBounds;
            if (!scene || !activePath ||
                activePath->GetType() != RenderPathType::Forward ||
                !scene->TryGetWorldBounds(sceneBounds))
            {
                FinishTaaDisocclusionSmokeTest(
                    false, "scene bounds unavailable for camera cut");
                return;
            }

            const glm::vec3 oldCameraPosition = camera.GetPosition();
            camera.FrameBounds(sceneBounds);
            m_TaaSmokeCameraCutDistance =
                glm::distance(oldCameraPosition, camera.GetPosition());
            if (m_TaaSmokeCameraCutDistance < 1.0f)
            {
                FinishTaaDisocclusionSmokeTest(
                    false, "Frame Scene did not move the camera enough to test a cut");
                return;
            }
            activePath->InvalidateHistory();
            if (activePath->HasUsableHistory("TAAOutput"))
            {
                FinishTaaDisocclusionSmokeTest(
                    false, "TAA history remained usable after camera cut");
                return;
            }
            if (!Renderer::Get().RequestFrameCapture(
                    m_TaaSmokeCameraCutCapturePath))
            {
                FinishTaaDisocclusionSmokeTest(
                    false, "camera-cut capture request was rejected");
                return;
            }
            m_TaaSmokeState =
                TaaDisocclusionSmokeState::WaitingForCameraCutCapture;
            m_TaaSmokeStateFrameCount = 0;
            CH_CORE_INFO("TAA smoke: Frame Scene camera cut moved {:.2f} units",
                         m_TaaSmokeCameraCutDistance);
            return;
        }
        case TaaDisocclusionSmokeState::WaitingForCameraCutCapture:
        {
            if (!std::filesystem::exists(m_TaaSmokeCameraCutCapturePath))
                return;

            m_TaaSmokeCameraCutStatistics = AnalyzeTemporalHistoryPng(
                m_TaaSmokeCameraCutCapturePath.string());
            if (!m_TaaSmokeCameraCutStatistics.success)
            {
                FinishTaaDisocclusionSmokeTest(
                    false, m_TaaSmokeCameraCutStatistics.error);
                return;
            }
            const VkExtent2D extent =
                Application::Get().GetContext()->GetSwapChainExtent();
            if (!RejectsPriorHistory(m_TaaSmokeCameraCutStatistics,
                                     extent.width, extent.height))
            {
                FinishTaaDisocclusionSmokeTest(
                    false, "first camera-cut frame did not reject prior history");
                return;
            }
            m_TaaSmokeWarmupFrameCount = 0;
            m_TaaSmokeState =
                TaaDisocclusionSmokeState::WarmingUpAfterCameraCut;
            m_TaaSmokeStateFrameCount = 0;
            CH_CORE_INFO("TAA smoke: camera cut rejected history; warming up");
            return;
        }
        case TaaDisocclusionSmokeState::WarmingUpAfterCameraCut:
        {
            if (!IsReadyForCapture(scene, activePath, sceneReady, true) ||
                activePath->GetType() != RenderPathType::Forward)
                return;

            if (++m_TaaSmokeWarmupFrameCount < 32)
                return;
            if (!Renderer::Get().RequestFrameCapture(
                    m_TaaSmokeCameraRecoveredCapturePath))
            {
                FinishTaaDisocclusionSmokeTest(
                    false, "camera-recovery capture request was rejected");
                return;
            }
            m_TaaSmokeState =
                TaaDisocclusionSmokeState::WaitingForCameraRecoveredCapture;
            m_TaaSmokeStateFrameCount = 0;
            return;
        }
        case TaaDisocclusionSmokeState::WaitingForCameraRecoveredCapture:
        {
            if (!std::filesystem::exists(m_TaaSmokeCameraRecoveredCapturePath))
                return;

            m_TaaSmokeCameraRecoveredStatistics = AnalyzeTemporalHistoryPng(
                m_TaaSmokeCameraRecoveredCapturePath.string());
            if (!m_TaaSmokeCameraRecoveredStatistics.success)
            {
                FinishTaaDisocclusionSmokeTest(
                    false, m_TaaSmokeCameraRecoveredStatistics.error);
                return;
            }
            if (!HasRecoveredHistory(m_TaaSmokeCameraRecoveredStatistics,
                                     m_TaaSmokeCameraCutStatistics.width,
                                     m_TaaSmokeCameraCutStatistics.height))
            {
                FinishTaaDisocclusionSmokeTest(
                    false, "TAA history did not recover after camera cut");
                return;
            }
            FinishTaaDisocclusionSmokeTest(
                true,
                "stable history, disocclusion, resize, path switch, scene replacement, and camera cut verified");
            return;
        }
        case TaaDisocclusionSmokeState::Disabled:
        case TaaDisocclusionSmokeState::Finished:
            return;
    }
}

void EditorAutomationController::InitializeRenderPathSmokeTest()
{
    m_RenderPathSmokeOutputDirectory =
        MakeSmokeOutputDirectory("render-path-smoke-results");
    m_RenderPathSmokeCapturePaths = {
        m_RenderPathSmokeOutputDirectory / "forward.png",
        m_RenderPathSmokeOutputDirectory / "hybrid.png",
        m_RenderPathSmokeOutputDirectory / "ray-tracing.png"};
    m_RenderPathSmokeResizedCapturePath =
        m_RenderPathSmokeOutputDirectory / "ray-tracing-resized.png";
    m_SvgfSwitchCapturePaths = {
        m_RenderPathSmokeOutputDirectory / "hybrid-spatial-only.png",
        m_RenderPathSmokeOutputDirectory / "hybrid-temporal-only.png",
        m_RenderPathSmokeOutputDirectory / "hybrid-temporal-spatial.png"};

    std::error_code directoryError;
    std::filesystem::create_directories(
        m_RenderPathSmokeOutputDirectory, directoryError);
    if (directoryError)
    {
        CH_CORE_ERROR("Render path smoke test could not create {}: {}",
                      m_RenderPathSmokeOutputDirectory.string(),
                      directoryError.message());
        m_RenderPathSmokeState = RenderPathSmokeState::Finished;
        Application::Get().Close(1);
        return;
    }

    m_RenderPathSmokePathIndex = 0;
    m_RenderPathSmokeState = RenderPathSmokeState::WaitingForScene;
    m_RenderPathSmokeStateFrameCount = 0;
    CH_CORE_INFO("Render path smoke test started; output: {}",
                 m_RenderPathSmokeOutputDirectory.string());
}

void EditorAutomationController::RequestCurrentRenderPath()
{
    const RenderPathType targetPath =
        m_RenderPathSmokePaths[m_RenderPathSmokePathIndex];
    Application::Get().SwitchRenderPath(targetPath);
    m_RenderPathSmokeState = RenderPathSmokeState::WaitingForPath;
    m_RenderPathSmokeStateFrameCount = 0;
    CH_CORE_INFO("Render path smoke: requested {} path",
                 RenderPathTypeToString(targetPath));
}

void EditorAutomationController::RequestSvgfSmokeSwitch(
    RenderPath* activePath, RenderFlags& renderFlags)
{
    const SvgfSmokeMode mode = m_SvgfSwitchModes[m_SvgfSwitchIndex];
    const RenderFlags previousFlags = renderFlags;
    renderFlags = WithSvgfSmokeMode(renderFlags, mode);
    if (ClassifyRenderFlagChanges(previousFlags ^ renderFlags) !=
        RenderSettingsChangeImpact::GraphRebuild)
    {
        FinishRenderPathSmokeTest(false,
                                  "SVGF mode switch did not require graph rebuild");
        return;
    }

    activePath->RequestGraphRebuild();
    m_RenderPathSmokeState = RenderPathSmokeState::WarmingUpSvgfSwitch;
    m_RenderPathSmokeWarmupFrameCount = 0;
    m_RenderPathSmokeStateFrameCount = 0;
    CH_CORE_INFO("Render path smoke: switched Hybrid SVGF to {}",
                 SvgfSmokeModeName(mode));
}

void EditorAutomationController::FinishRenderPathSmokeTest(
    bool passed, const std::string& reason)
{
    const std::filesystem::path resultPath =
        m_RenderPathSmokeOutputDirectory / "result.txt";
    std::ofstream resultFile(resultPath);
    if (resultFile)
    {
        resultFile << (passed ? "PASS" : "FAIL") << '\n'
                   << "reason=" << reason << '\n'
                   << "svgfSmokeMode="
                   << SvgfSmokeModeName(m_Options.svgfSmokeMode) << '\n'
                   << "svgfToggleSmoke="
                   << (m_Options.svgfToggleSmokeTest ? "true" : "false")
                   << '\n';

        for (size_t index = 0; index < m_RenderPathSmokePaths.size(); ++index)
        {
            const char* pathName =
                RenderPathTypeToString(m_RenderPathSmokePaths[index]);
            resultFile << pathName << "Capture="
                       << m_RenderPathSmokeCapturePaths[index].string()
                       << '\n'
                       << pathName << "Bytes="
                       << m_RenderPathSmokeCaptureSizes[index] << '\n';

            if (index > 0 &&
                m_RenderPathSmokeComparisons[index].success)
            {
                resultFile << pathName << "VsForwardDifferentPixels="
                           << m_RenderPathSmokeComparisons[index]
                                  .differentPixelCount
                           << '\n'
                           << pathName << "VsForwardMaxChannelDifference="
                           << static_cast<uint32_t>(
                                  m_RenderPathSmokeComparisons[index]
                                      .maxChannelDifference)
                           << '\n'
                           << pathName << "VsForwardRmse=" << std::fixed
                           << std::setprecision(6)
                           << m_RenderPathSmokeComparisons[index].rmse
                           << '\n';
            }
        }
        if (m_Options.svgfToggleSmokeTest)
        {
            resultFile << "NoiseRegion=635,710,150,140\n"
                       << "NoiseMetric=mean absolute red-channel residual from four adjacent pixels; Box front face only\n";
            for (size_t index = 0; index < m_SvgfSwitchModes.size(); ++index)
            {
                resultFile << SvgfSmokeModeName(m_SvgfSwitchModes[index])
                           << "Capture="
                           << m_SvgfSwitchCapturePaths[index].string() << '\n'
                           << SvgfSmokeModeName(m_SvgfSwitchModes[index])
                           << "Bytes=" << m_SvgfSwitchCaptureSizes[index]
                           << '\n';
                const auto& metric = m_SvgfSwitchNoiseMetrics[index];
                if (metric.success)
                {
                    resultFile << SvgfSmokeModeName(m_SvgfSwitchModes[index])
                               << "HighFrequencyResidual=" << std::fixed
                               << std::setprecision(6)
                               << metric.meanAbsoluteResidual << '\n'
                               << SvgfSmokeModeName(m_SvgfSwitchModes[index])
                               << "MeanRed=" << metric.meanRgb[0] << '\n'
                               << SvgfSmokeModeName(m_SvgfSwitchModes[index])
                               << "SampleCount=" << metric.sampleCount << '\n';
                }
                else if (m_SvgfSwitchCaptureSizes[index] != 0)
                {
                    resultFile << SvgfSmokeModeName(m_SvgfSwitchModes[index])
                               << "NoiseMetricError=" << metric.error << '\n';
                }
            }
        }
        if (m_RenderPathSmokeResizedWidth != 0)
        {
            resultFile << "ResizedCapture="
                       << m_RenderPathSmokeResizedCapturePath.string() << '\n'
                       << "ResizedWidth=" << m_RenderPathSmokeResizedWidth
                       << '\n'
                       << "ResizedHeight=" << m_RenderPathSmokeResizedHeight
                       << '\n';
        }
    }

    if (passed)
    {
        CH_CORE_INFO("Render path smoke test PASSED: {}", reason);
    }
    else
    {
        CH_CORE_ERROR("Render path smoke test FAILED: {}", reason);
    }
    CH_CORE_INFO("Render path smoke test result: {}", resultPath.string());

    m_RenderPathSmokeState = RenderPathSmokeState::Finished;
    Application::Get().Close(passed ? 0 : 1);
}

void EditorAutomationController::UpdateRenderPathSmokeTest(
    Scene* scene, RenderPath* activePath, bool sceneReady, bool sceneFailed,
    RenderFlags& renderFlags)
{
    if (m_RenderPathSmokeState == RenderPathSmokeState::Disabled ||
        m_RenderPathSmokeState == RenderPathSmokeState::Finished)
    {
        return;
    }

    ++m_RenderPathSmokeStateFrameCount;
    if (m_RenderPathSmokeStateFrameCount > 900)
    {
        FinishRenderPathSmokeTest(
            false, "timed out while waiting for the current test phase");
        return;
    }

    if (sceneFailed)
    {
        FinishRenderPathSmokeTest(false, "benchmark scene failed to load");
        return;
    }

    const bool ready =
        IsReadyForCapture(scene, activePath, sceneReady, true);
    const RenderPathType targetPath =
        m_RenderPathSmokePaths[m_RenderPathSmokePathIndex];

    switch (m_RenderPathSmokeState)
    {
        case RenderPathSmokeState::WaitingForScene:
        {
            if (!ready)
            {
                return;
            }

            RequestCurrentRenderPath();
            return;
        }
        case RenderPathSmokeState::WaitingForPath:
        {
            if (!activePath || activePath->GetType() != targetPath || !ready)
            {
                return;
            }

            m_RenderPathSmokeState = RenderPathSmokeState::WarmingUp;
            m_RenderPathSmokeWarmupFrameCount = 0;
            m_RenderPathSmokeStateFrameCount = 0;
            CH_CORE_INFO("Render path smoke: {} ready; warming up",
                         RenderPathTypeToString(targetPath));
            return;
        }
        case RenderPathSmokeState::WarmingUp:
        {
            if (!activePath || activePath->GetType() != targetPath)
            {
                FinishRenderPathSmokeTest(
                    false, "active render path changed during warm-up");
                return;
            }
            if (!ready)
            {
                return;
            }

            constexpr uint32_t WarmupFrameCount = 8;
            ++m_RenderPathSmokeWarmupFrameCount;
            if (m_RenderPathSmokeWarmupFrameCount < WarmupFrameCount)
            {
                return;
            }

            if (m_Options.svgfSmokeMode != SvgfSmokeMode::None &&
                targetPath == RenderPathType::Hybrid)
            {
                if (!GraphMatchesSvgfSmokeMode(activePath->GetRenderGraph(),
                                               m_Options.svgfSmokeMode))
                {
                    FinishRenderPathSmokeTest(
                        false,
                        "Hybrid SVGF graph has unexpected resources");
                    return;
                }
                if (m_Options.svgfSmokeMode != SvgfSmokeMode::SpatialOnly &&
                    !HasCompleteSvgfHistory(*activePath))
                {
                    FinishRenderPathSmokeTest(
                        false, "Hybrid SVGF history did not warm up");
                    return;
                }
            }

            if (!Renderer::Get().RequestFrameCapture(
                    m_RenderPathSmokeCapturePaths[
                        m_RenderPathSmokePathIndex]))
            {
                FinishRenderPathSmokeTest(
                    false, "render path capture request was rejected");
                return;
            }

            m_RenderPathSmokeState = RenderPathSmokeState::WaitingForCapture;
            m_RenderPathSmokeStateFrameCount = 0;
            CH_CORE_INFO("Render path smoke: {} capture requested",
                         RenderPathTypeToString(targetPath));
            return;
        }
        case RenderPathSmokeState::WaitingForCapture:
        {
            if (!activePath || activePath->GetType() != targetPath)
            {
                FinishRenderPathSmokeTest(
                    false, "active render path changed before capture completed");
                return;
            }

            const std::filesystem::path& capturePath =
                m_RenderPathSmokeCapturePaths[m_RenderPathSmokePathIndex];
            if (!std::filesystem::exists(capturePath))
            {
                return;
            }

            std::error_code fileError;
            const uintmax_t captureSize =
                std::filesystem::file_size(capturePath, fileError);
            if (fileError || captureSize == 0)
            {
                FinishRenderPathSmokeTest(
                    false, "render path capture file is empty or unreadable");
                return;
            }
            m_RenderPathSmokeCaptureSizes[m_RenderPathSmokePathIndex] =
                captureSize;

            ImageComparisonResult comparison;
            if (m_RenderPathSmokePathIndex == 0)
            {
                comparison = ComparePngFiles(
                    capturePath.string(), capturePath.string());
            }
            else
            {
                comparison = ComparePngFiles(
                    m_RenderPathSmokeCapturePaths[0].string(),
                    capturePath.string());
            }
            if (!comparison.success)
            {
                FinishRenderPathSmokeTest(false, comparison.error);
                return;
            }
            if (!HasVisibleScenePixels(capturePath))
            {
                FinishRenderPathSmokeTest(
                    false, "render path capture contains no visible scene");
                return;
            }
            m_RenderPathSmokeComparisons[m_RenderPathSmokePathIndex] =
                comparison;

            CH_CORE_INFO(
                "Render path smoke: {} captured ({} bytes)",
                RenderPathTypeToString(targetPath), captureSize);

            if (m_Options.svgfToggleSmokeTest &&
                targetPath == RenderPathType::Hybrid)
            {
                m_SvgfSwitchIndex = 0;
                RequestSvgfSmokeSwitch(activePath, renderFlags);
                return;
            }

            if (m_RenderPathSmokePathIndex + 1 ==
                m_RenderPathSmokePaths.size())
            {
                const VkExtent2D extent =
                    Application::Get().GetContext()->GetSwapChainExtent();
                m_RenderPathSmokeOriginalWidth = extent.width;
                m_RenderPathSmokeOriginalHeight = extent.height;
                glfwSetWindowSize(
                    Application::Get().GetWindow().GetNativeWindow(),
                    1280, 720);
                m_RenderPathSmokeState = RenderPathSmokeState::WaitingForResize;
                m_RenderPathSmokeStateFrameCount = 0;
                CH_CORE_INFO("Render path smoke: requested window resize");
                return;
            }

            ++m_RenderPathSmokePathIndex;
            RequestCurrentRenderPath();
            return;
        }
        case RenderPathSmokeState::WarmingUpSvgfSwitch:
        {
            if (!ready || !activePath ||
                activePath->GetType() != RenderPathType::Hybrid)
                return;

            if (++m_RenderPathSmokeWarmupFrameCount < 8)
                return;

            const SvgfSmokeMode mode = m_SvgfSwitchModes[m_SvgfSwitchIndex];
            if (!GraphMatchesSvgfSmokeMode(activePath->GetRenderGraph(), mode))
            {
                FinishRenderPathSmokeTest(
                    false, "Hybrid SVGF graph did not match switched mode");
                return;
            }
            if (mode != SvgfSmokeMode::SpatialOnly &&
                !HasCompleteSvgfHistory(*activePath))
            {
                FinishRenderPathSmokeTest(
                    false, "switched SVGF history did not warm up");
                return;
            }
            if (!Renderer::Get().RequestFrameCapture(
                    m_SvgfSwitchCapturePaths[m_SvgfSwitchIndex]))
            {
                FinishRenderPathSmokeTest(
                    false, "SVGF switch capture request was rejected");
                return;
            }
            m_RenderPathSmokeState =
                RenderPathSmokeState::WaitingForSvgfSwitchCapture;
            m_RenderPathSmokeStateFrameCount = 0;
            return;
        }
        case RenderPathSmokeState::WaitingForSvgfSwitchCapture:
        {
            const std::filesystem::path& capturePath =
                m_SvgfSwitchCapturePaths[m_SvgfSwitchIndex];
            if (!std::filesystem::exists(capturePath))
                return;

            std::error_code fileError;
            const uintmax_t captureSize =
                std::filesystem::file_size(capturePath, fileError);
            if (fileError || captureSize == 0 ||
                !HasVisibleScenePixels(capturePath))
            {
                FinishRenderPathSmokeTest(
                    false, "switched SVGF capture has no visible scene");
                return;
            }
            m_SvgfSwitchCaptureSizes[m_SvgfSwitchIndex] = captureSize;
            ImageHighFrequencyResult metric = AnalyzeHighFrequencyPng(
                capturePath.string(), {635, 710, 150, 140});
            if (metric.success &&
                (metric.meanRgb[0] <= metric.meanRgb[1] + 40.0 ||
                 metric.meanRgb[0] <= metric.meanRgb[2] + 40.0))
            {
                metric.success = false;
                metric.error = "Box front-face region is not red-dominant";
            }
            m_SvgfSwitchNoiseMetrics[m_SvgfSwitchIndex] = metric;
            if (!metric.success)
                CH_CORE_WARN("Render path smoke: {} noise metric unavailable: {}",
                             SvgfSmokeModeName(
                                 m_SvgfSwitchModes[m_SvgfSwitchIndex]),
                             metric.error);
            CH_CORE_INFO("Render path smoke: {} captured ({} bytes)",
                         SvgfSmokeModeName(
                             m_SvgfSwitchModes[m_SvgfSwitchIndex]),
                         captureSize);

            if (++m_SvgfSwitchIndex < m_SvgfSwitchModes.size())
            {
                RequestSvgfSmokeSwitch(activePath, renderFlags);
                return;
            }
            ++m_RenderPathSmokePathIndex;
            RequestCurrentRenderPath();
            return;
        }
        case RenderPathSmokeState::WaitingForResize:
        {
            int framebufferWidth = 0;
            int framebufferHeight = 0;
            glfwGetFramebufferSize(
                Application::Get().GetWindow().GetNativeWindow(),
                &framebufferWidth, &framebufferHeight);
            if (framebufferWidth <= 0 || framebufferHeight <= 0)
                return;

            const VkExtent2D extent =
                Application::Get().GetContext()->GetSwapChainExtent();
            if (extent.width == m_RenderPathSmokeOriginalWidth &&
                extent.height == m_RenderPathSmokeOriginalHeight)
                return;
            if (extent.width != static_cast<uint32_t>(framebufferWidth) ||
                extent.height != static_cast<uint32_t>(framebufferHeight) ||
                !ready)
                return;

            m_RenderPathSmokeResizedWidth = extent.width;
            m_RenderPathSmokeResizedHeight = extent.height;
            m_RenderPathSmokeWarmupFrameCount = 0;
            m_RenderPathSmokeStateFrameCount = 0;
            m_RenderPathSmokeState = RenderPathSmokeState::WarmingUpResized;
            CH_CORE_INFO("Render path smoke: swapchain resized to {}x{}",
                         extent.width, extent.height);
            return;
        }
        case RenderPathSmokeState::WarmingUpResized:
        {
            if (!ready || !activePath || activePath->GetType() != targetPath)
                return;

            if (++m_RenderPathSmokeWarmupFrameCount < 8)
                return;

            if (!Renderer::Get().RequestFrameCapture(
                    m_RenderPathSmokeResizedCapturePath))
            {
                FinishRenderPathSmokeTest(
                    false, "resized frame capture request was rejected");
                return;
            }
            m_RenderPathSmokeState =
                RenderPathSmokeState::WaitingForResizedCapture;
            m_RenderPathSmokeStateFrameCount = 0;
            return;
        }
        case RenderPathSmokeState::WaitingForResizedCapture:
        {
            if (!std::filesystem::exists(m_RenderPathSmokeResizedCapturePath))
                return;

            int pngWidth = 0;
            int pngHeight = 0;
            int channels = 0;
            if (!stbi_info(m_RenderPathSmokeResizedCapturePath.string().c_str(),
                           &pngWidth, &pngHeight, &channels) ||
                pngWidth != static_cast<int>(m_RenderPathSmokeResizedWidth) ||
                pngHeight != static_cast<int>(m_RenderPathSmokeResizedHeight))
            {
                FinishRenderPathSmokeTest(
                    false, "resized capture does not match swapchain extent");
                return;
            }
            if (!HasVisibleScenePixels(m_RenderPathSmokeResizedCapturePath))
            {
                FinishRenderPathSmokeTest(
                    false, "resized capture contains no visible scene");
                return;
            }

            FinishRenderPathSmokeTest(
                true,
                m_Options.svgfSmokeMode != SvgfSmokeMode::None
                    ? "SVGF graph and all render-path captures passed"
                    : "Forward, Hybrid, RayTracing, and resized RayTracing rendered valid captures");
            return;
        }
        case RenderPathSmokeState::Disabled:
        case RenderPathSmokeState::Finished:
            return;
    }
}

} // namespace Chimera
