#include "pch.h"
#include "EditorLayer.h"
#include "Core/Application.h"
#include "Core/ImGuiLayer.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/Backend/Renderer.h"
#include "Utils/VulkanBarrier.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Benchmark/BenchmarkCsvWriter.h"
#include "Renderer/Capture/ImageRegression.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Pipelines/RenderPath.h"
#include "Scene/Scene.h"
#include "Core/Input.h"
#include "Scene/Model.h"
#include "Assets/AssetImporter.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <shellapi.h>
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <limits>
#include <sstream>
#include "Renderer/Pipelines/RenderPathFactory.h"
#include "Renderer/RenderState.h"

namespace Chimera
{
namespace
{
constexpr const char* BenchmarkScenePresetName = "smoke-test-box-v1";

void ApplyBenchmarkCameraPreset(EditorCamera& camera)
{
    camera.SetFocalPoint({-5.944f, 1.950f, -1.602f});
    camera.SetDistance(12.426f);
    camera.SetPitch(-0.032f);
    camera.SetYaw(-1.396f);
}

void ApplyBenchmarkLightingPreset(Scene& scene)
{
    auto& light = scene.GetMainLight();
    light.direction =
        glm::vec4(glm::normalize(glm::vec3(0.085f, -0.987f, 0.139f)),
                  0.5f);
    light.color = glm::vec4(1.0f, 0.95f, 0.8f, 5.0f);
}

std::filesystem::path MakeBenchmarkCsvPath()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t timestamp = std::chrono::system_clock::to_time_t(now);
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) %
        1000;
    std::tm localTime{};
    localtime_s(&localTime, &timestamp);

    std::ostringstream filename;
    filename << "gpu-benchmark-" << std::put_time(&localTime, "%Y%m%d-%H%M%S")
             << '-' << std::setfill('0') << std::setw(3)
             << milliseconds.count() << ".csv";

    return std::filesystem::current_path() / "benchmark-results" /
           filename.str();
}

std::filesystem::path MakeFrameCapturePath()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t timestamp =
        std::chrono::system_clock::to_time_t(now);

    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) %
        1000;

    std::tm localTime{};
    localtime_s(&localTime, &timestamp);

    std::ostringstream filename;
    filename << "frame-"
             << std::put_time(
                    &localTime, "%Y%m%d-%H%M%S")
             << '-'
             << std::setfill('0')
             << std::setw(3)
             << milliseconds.count()
             << ".png";

    return std::filesystem::current_path() /
           "frame-captures" /
           filename.str();
}

std::filesystem::path MakeRegressionBaselinePath()
{
    return std::filesystem::current_path() / "frame-captures" /
           "regression-baseline.png";
}

std::filesystem::path MakeRegressionSignaturePath()
{
    return std::filesystem::current_path() / "frame-captures" /
           "regression-baseline.txt";
}

std::string MakeRegressionSignature(
    const RenderPath& renderPath, const EditorCamera& camera,
    RenderFlags renderFlags, DisplayMode displayMode, float exposure,
    float ambientStrength, const glm::vec4& clearColor, float lightRadius,
    const std::string& assetPath, uint32_t warmupFrameCount, Scene* scene)
{
    std::ostringstream signature;
    signature << std::setprecision(std::numeric_limits<float>::max_digits10)
              << "version=4\n"
              << "temporalSequence=local-halton-and-gpu-seed-v1\n"
              << "warmupFrames=" << warmupFrameCount << '\n'
              << "renderPath=" << RenderPathTypeToString(renderPath.GetType())
              << '\n'
              << "width=" << renderPath.GetRenderGraph().GetWidth() << '\n'
              << "height=" << renderPath.GetRenderGraph().GetHeight() << '\n'
              << "renderFlags=" << static_cast<uint32_t>(renderFlags) << '\n'
              << "displayMode=" << static_cast<uint32_t>(displayMode) << '\n'
              << "exposure=" << exposure << '\n'
              << "ambientStrength=" << ambientStrength << '\n'
              << "clearColor=" << clearColor.x << ',' << clearColor.y << ','
              << clearColor.z << ',' << clearColor.w << '\n'
              << "lightRadius=" << lightRadius << '\n'
              << "assetPath=" << assetPath << '\n'
              << "cameraFocalPoint=" << camera.GetFocalPoint().x << ','
              << camera.GetFocalPoint().y << ',' << camera.GetFocalPoint().z
              << '\n'
              << "cameraDistance=" << camera.GetDistance() << '\n'
              << "cameraPitch=" << camera.GetPitch() << '\n'
              << "cameraYaw=" << camera.GetYaw() << '\n'
              << "cameraFov=" << camera.GetFOV() << '\n';

    if (scene)
    {
        const std::vector<Entity>& entities = scene->GetEntities();
        signature << "entityCount=" << entities.size() << '\n';
        for (size_t index = 0; index < entities.size(); ++index)
        {
            const Entity& entity = entities[index];
            signature << "entity[" << index << "].name=" << entity.name
                      << '\n'
                      << "entity[" << index << "].position="
                      << entity.transform.position.x << ','
                      << entity.transform.position.y << ','
                      << entity.transform.position.z << '\n'
                      << "entity[" << index << "].rotation="
                      << entity.transform.rotation.x << ','
                      << entity.transform.rotation.y << ','
                      << entity.transform.rotation.z << '\n'
                      << "entity[" << index << "].scale="
                      << entity.transform.scale.x << ','
                      << entity.transform.scale.y << ','
                      << entity.transform.scale.z << '\n';
        }

        const std::vector<Light>& lights = scene->GetLights();
        signature << "lightCount=" << lights.size() << '\n';
        for (size_t index = 0; index < lights.size(); ++index)
        {
            const Light& light = lights[index];
            signature << "light[" << index << "].position="
                      << light.position.x << ',' << light.position.y << ','
                      << light.position.z << ',' << light.position.w << '\n'
                      << "light[" << index << "].direction="
                      << light.direction.x << ',' << light.direction.y << ','
                      << light.direction.z << ',' << light.direction.w << '\n'
                      << "light[" << index << "].color=" << light.color.x
                      << ',' << light.color.y << ',' << light.color.z << ','
                      << light.color.w << '\n';
        }
    }
    return signature.str();
}

std::filesystem::path MakeDifferencePath(
    const std::filesystem::path& actualPath)
{
    return actualPath.parent_path() /
           (actualPath.stem().string() + "-diff.png");
}

} // namespace

EditorLayer::EditorLayer()
    : Layer("EditorLayer"), m_EditorCamera(45.0f, 1.778f, 0.1f, 1000.0f)
{
    m_ShowControlPanel = true;

    auto& app = Application::Get();
    m_ViewportSize = {(float)app.GetWindow().GetWidth(),
                      (float)app.GetWindow().GetHeight()};

    m_EditorCamera.SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
    ApplyBenchmarkCameraPreset(m_EditorCamera);
    m_EditorCamera.SetFOV(45.0f);

    m_RenderFlags = RenderFlags_LightBit | RenderFlags_ShadowBit |
                    RenderFlags_SVGFTemporalBit | RenderFlags_SVGFSpatialBit |
                    RenderFlags_IBLBit;

    m_AmbientStrength = 0.0f;
    m_Exposure = 1.0f;
    m_LightRadius = 0.5f;

    auto scene = std::make_shared<Scene>(app.GetContext());

    ApplyBenchmarkLightingPreset(*scene);

    ResourceManager::Get().SetActiveScene(scene);

    Application::Get().SwitchRenderPath(RenderPathType::Hybrid);
}

void EditorLayer::OnAttach()
{
    RefreshAssetList();

    m_ActiveAssetPath = Application::Get().GetSpecification().AssetDir +
                        "models/smoke_test/Box.gltf";
    m_BenchmarkSceneState = BenchmarkSceneState::Preparing;
    m_BenchmarkPrepareStartFrame =
        Application::Get().GetTotalFrameCount();
    ResourceManager::Get().LoadScene(m_ActiveAssetPath);

        /*
ResourceManager::Get().LoadHDR(
Application::Get().GetSpecification().AssetDir +
"textures/hdr/dreifaltigkeitsberg_2k.hdr");
*/
}

void EditorLayer::OnDetach() {}

void EditorLayer::RefreshAssetList()
{
    // Refresh Model List
    {
        m_AvailableModels.clear();
        std::string rootPath =
            Application::Get().GetSpecification().AssetDir + "models";
        auto models = AssetImporter::GetAvailableModels(rootPath);
        for (auto& model : models)
            m_AvailableModels.push_back({model.Name, model.Path});
    }

    // Refresh HDR List
    {
        m_AvailableHDRs.clear();
        std::string assetDir = Application::Get().GetSpecification().AssetDir;
        std::string hdrDir = assetDir + "textures/hdr";
        auto hdrs = AssetImporter::GetAvailableHDRs(hdrDir);
        for (auto& hdr : hdrs) m_AvailableHDRs.push_back({hdr.Name, hdr.Path});
    }
}

void EditorLayer::ClearScene()
{
    InvalidateBenchmarkScenePreset();
    ResourceManager::Get().ClearScene();
    m_SelectedInstanceIndex = -1;
    m_SelectedAssetIndex = -1;
    m_ActiveAssetPath = "";
}

void EditorLayer::PrepareBenchmarkScene()
{
    if (RenderPath* activePath = GetRenderPath())
    {
        activePath->ResetBenchmark();
    }

    ApplyBenchmarkCameraPreset(m_EditorCamera);
    m_Exposure = 1.0f;
    m_AmbientStrength = 0.0f;
    m_LightRadius = 0.5f;

    ResourceManager::Get().ClearScene();

    m_ActiveAssetPath = Application::Get().GetSpecification().AssetDir +
                        "models/smoke_test/Box.gltf";
    ResourceManager::Get().LoadScene(m_ActiveAssetPath);
    Application::Get().QueueEvent(
        []()
        {
            if (Scene* scene = ResourceManager::Get().GetActiveScene())
            {
                ApplyBenchmarkLightingPreset(*scene);
            }
        });

    m_SelectedInstanceIndex = -1;
    m_SelectedAssetIndex = -1;
    m_BenchmarkSceneState = BenchmarkSceneState::Preparing;
    m_BenchmarkPrepareStartFrame =
        Application::Get().GetTotalFrameCount();
}

void EditorLayer::UpdateBenchmarkSceneState()
{
    if (m_BenchmarkSceneState != BenchmarkSceneState::Preparing)
    {
        return;
    }

    if (ResourceManager::Get().HasPendingModelLoads())
    {
        return;
    }

    Scene* scene = GetActiveSceneRaw();
    if (scene && !scene->GetEntities().empty())
    {
        m_BenchmarkSceneState = BenchmarkSceneState::Ready;
        return;
    }

    if (Application::Get().GetTotalFrameCount() >
        m_BenchmarkPrepareStartFrame)
    {
        m_BenchmarkSceneState = BenchmarkSceneState::Failed;
    }
}

void EditorLayer::InvalidateBenchmarkScenePreset()
{
    m_BenchmarkSceneState = BenchmarkSceneState::Unprepared;
    if (RenderPath* activePath = GetRenderPath())
    {
        activePath->ResetBenchmark();
    }
}

void EditorLayer::OnUpdate(Timestep ts)
{
    m_AverageFrameTime = ts.GetMilliseconds();
    m_AverageFPS = 1.0f / ts.GetSeconds();

    auto& window = Application::Get().GetWindow();
    float winW = (float)window.GetWidth();
    float winH = (float)window.GetHeight();

    if (winW > 0 && (std::abs(winW - m_ViewportSize.x) > 0.1f ||
                     std::abs(winH - m_ViewportSize.y) > 0.1f))
    {
        m_EditorCamera.SetViewportSize(winW, winH);
        m_ViewportSize = {winW, winH};
    }

    bool uiHovered = ImGui::GetIO().WantCaptureMouse;
    RenderPath* activePath = GetRenderPath();
    const bool benchmarkRunning =
        activePath && activePath->GetBenchmarkRecorder().IsRunning();
    const bool captureSequenceRunning =
        m_WarmupCaptureAction == FrameCaptureAction::Baseline ||
        m_WarmupCaptureAction == FrameCaptureAction::Regression ||
        m_PendingCaptureAction == FrameCaptureAction::Baseline ||
        m_PendingCaptureAction == FrameCaptureAction::Regression;
    const bool allowCameraInput =
        !uiHovered && !benchmarkRunning && !captureSequenceRunning;
    m_EditorCamera.OnUpdate(ts, allowCameraInput, allowCameraInput);
    const uint32_t temporalFrameIndex =
        captureSequenceRunning ? m_CaptureTemporalFrameIndex++
                               : Application::Get().GetTotalFrameCount();
    m_EditorCamera.UpdateTAAState(temporalFrameIndex,
                                  (m_RenderFlags & RenderFlags_TAABit) != 0);

    if (auto scene = GetActiveSceneRaw()) scene->OnUpdate(ts.GetSeconds());
    UpdateBenchmarkSceneState();

    AppFrameContext context;
    context.View = m_EditorCamera.GetViewMatrix();
    context.Projection = m_EditorCamera.GetProjection();
    context.CamFrustum = m_EditorCamera.GetFrustum();
    context.PrevView = m_EditorCamera.GetPrevView();
    context.PrevProj = m_EditorCamera.GetPrevProj();
    context.Jitter = m_EditorCamera.GetJitter();
    context.PrevJitter = m_EditorCamera.GetPrevJitter();
    context.CameraPosition = m_EditorCamera.GetPosition();
    context.ViewportSize = m_ViewportSize;
    context.FrameIndex = temporalFrameIndex;
    context.RenderFlags = m_RenderFlags;
    context.Exposure = m_Exposure;
    context.DisplayMode = m_DisplayMode;
    context.AmbientStrength = m_AmbientStrength;
    context.ClearColor = m_ClearColor;
    context.LightRadius = m_LightRadius;

    Application::Get().SetFrameContext(context);
    Application::Get().SetActiveScene(GetActiveSceneRaw());
}

void EditorLayer::OnEvent(Event& e)
{
    RenderPath* activePath = GetRenderPath();
    const bool benchmarkRunning =
        activePath && activePath->GetBenchmarkRecorder().IsRunning();
    const bool captureSequenceRunning =
        m_WarmupCaptureAction == FrameCaptureAction::Baseline ||
        m_WarmupCaptureAction == FrameCaptureAction::Regression ||
        m_PendingCaptureAction == FrameCaptureAction::Baseline ||
        m_PendingCaptureAction == FrameCaptureAction::Regression;

    if (!ImGui::GetIO().WantCaptureMouse && !benchmarkRunning &&
        !captureSequenceRunning)
    {
        m_EditorCamera.OnEvent(e);
    }
}

void EditorLayer::UpdateFrameCaptureWarmup()
{
    if (m_WarmupCaptureAction == FrameCaptureAction::None ||
        !m_CaptureWarmup.IsActive())
    {
        return;
    }

    if (!m_CaptureWarmup.AdvanceAfterRenderedFrame())
    {
        return;
    }

    Renderer& renderer = Renderer::Get();
    if (renderer.RequestFrameCapture(m_WarmupCapturePath))
    {
        m_LastCapturePath = m_WarmupCapturePath;
        m_PendingCaptureAction = m_WarmupCaptureAction;
        m_CaptureStatus = "Warm-up complete; capture requested...";
    }
    else
    {
        m_CaptureSucceeded = false;
        m_CaptureStatusIsError = true;
        m_CaptureStatus = "Capture request rejected after warm-up";
        m_PendingRegressionSignature.clear();
    }

    m_WarmupCaptureAction = FrameCaptureAction::None;
    m_WarmupCapturePath.clear();
}

void EditorLayer::OnImGuiRender()
{
    UpdateFrameCaptureWarmup();

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("DockSpaceParent", nullptr, window_flags);
    ImGui::PopStyleVar();

    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    if (!ImGui::DockBuilderGetNode(dockspace_id))
    {
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);
        ImGuiID dock_main_id = dockspace_id;
        ImGuiID dock_right_id = ImGui::DockBuilderSplitNode(
            dock_main_id, ImGuiDir_Right, 0.20f, nullptr, &dock_main_id);
        ImGui::DockBuilderDockWindow("Control Panel", dock_right_id);
        ImGui::DockBuilderFinish(dockspace_id);
    }
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f),
                     ImGuiDockNodeFlags_PassthruCentralNode);

    DrawMenuBar();
    ImGui::Begin("Control Panel", &m_ShowControlPanel);
    DrawControlPanelContent(GetRenderPath());
    ImGui::End();
    ImGui::End();
}

void EditorLayer::DrawSceneHierarchy()
{
    Scene* scene = GetActiveSceneRaw();
    if (!scene) return;
    const auto& entities = scene->GetEntities();
    for (int i = 0; i < (int)entities.size(); i++)
    {
        ImGui::PushID(i);
        if (ImGui::Selectable(
                (entities[i].name + "##" + std::to_string(i)).c_str(),
                m_SelectedInstanceIndex == i))
            m_SelectedInstanceIndex = i;
        ImGui::PopID();
    }
    if (m_SelectedInstanceIndex >= 0)
    {
        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Remove Selected", ImVec2(-1, 0)))
        {
            GetActiveSceneRaw()->RemoveEntity(m_SelectedInstanceIndex);
            InvalidateBenchmarkScenePreset();
            m_SelectedInstanceIndex = -1;
        }
    }
}

void EditorLayer::DrawPropertiesPanel(RenderPath* activePath)
{
    Scene* scene = GetActiveSceneRaw();
    if (!scene || m_SelectedInstanceIndex < 0) return;
    const auto& entities = scene->GetEntities();
    auto& entity = entities[m_SelectedInstanceIndex];
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
    {
        bool changed = false;
        glm::vec3 pos = entity.transform.position;
        glm::vec3 rot = entity.transform.rotation;
        glm::vec3 scale = entity.transform.scale;
        if (ImGui::DragFloat3("Position", &pos.x, 0.1f)) changed = true;
        if (ImGui::DragFloat3("Rotation", &rot.x, 0.5f)) changed = true;
        if (ImGui::DragFloat3("Scale", &scale.x, 0.05f)) changed = true;
        if (changed)
        {
            scene->UpdateEntityTRS(m_SelectedInstanceIndex, pos, rot, scale);
            InvalidateBenchmarkScenePreset();
            if (activePath) activePath->OnSceneUpdated();
        }
    }
}

void EditorLayer::DrawMenuBar()
{
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("Refresh Assets")) RefreshAssetList();
        if (ImGui::MenuItem("Clear Scene")) ClearScene();
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) Application::Get().Close();
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void EditorLayer::DrawLightSettings(RenderPath* activePath)
{
    Scene* scene = GetActiveSceneRaw();
    if (!scene) return;

    if (!ImGui::TreeNodeEx("Sun & Environment Parameters",
                           ImGuiTreeNodeFlags_DefaultOpen))
        return;

    auto& light = scene->GetMainLight();
    bool changed = false;

    if (ImGui::DragFloat3("Direction", &light.direction.x, 0.01f, -1.0f, 1.0f))
    {
        light.direction = glm::vec4(glm::normalize(glm::vec3(light.direction)),
                                    light.direction.w);
        changed = true;
    }

    changed |= ImGui::ColorEdit3("Color", &light.color.x);

    float intensity = light.color.a;
    if (ImGui::DragFloat("Intensity", &intensity, 0.1f, 0.0f, 100.0f))
    {
        light.color.a = intensity;
        changed = true;
    }

    if (ImGui::SliderFloat("Light Radius (Soft Shadows)", &m_LightRadius, 0.0f,
                           0.5f))
    {
        light.direction.w = m_LightRadius;
        changed = true;
    }

    ImGui::Separator();
    changed |= ImGui::DragFloat("Exposure", &m_Exposure, 0.05f, 0.01f, 10.0f);
    changed |=
        ImGui::SliderFloat("Ambient Strength", &m_AmbientStrength, 0.0f, 2.0f);

    if (changed)
    {
        InvalidateBenchmarkScenePreset();
        if (activePath) activePath->OnSceneUpdated();
    }

    ImGui::TreePop();
}

void EditorLayer::DrawRenderPathPanel(RenderPath* activePath)
{
    RenderPathType currentType =
        activePath ? activePath->GetType() : RenderPathType::Forward;
    if (ImGui::BeginCombo("Active Path", RenderPathTypeToString(currentType)))
    {
        for (auto type : GetAllRenderPathTypes())
            if (ImGui::Selectable(RenderPathTypeToString(type),
                                  currentType == type))
                Application::Get().SwitchRenderPath(type);
        ImGui::EndCombo();
    }
    const char* displayModes[] = {"Final Color", "Albedo",   "Normal",
                                  "Material",    "Motion",   "Depth",
                                  "Shadow",      "AO",       "Reflection",
                                  "Diffuse GI",  "Emissive", "SVGF Variance"};
    int currentDisplayMode = (int)m_DisplayMode;
    if (ImGui::Combo("Display Mode", &currentDisplayMode, displayModes,
                     IM_ARRAYSIZE(displayModes)))
        m_DisplayMode = (DisplayMode)currentDisplayMode;
}

void EditorLayer::DrawFeatureToggles(RenderPath* activePath)
{
    if (!ImGui::CollapsingHeader("Render Feature Toggles",
                                 ImGuiTreeNodeFlags_DefaultOpen))
        return;
    bool changed = false;
    auto toggleFlag = [&](const char* label, RenderFlags flag)
    {
        bool enabled = (m_RenderFlags & flag) != 0;
        if (ImGui::Checkbox(label, &enabled))
        {
            if (enabled)
                m_RenderFlags |= flag;
            else
                m_RenderFlags &= ~flag;
            changed = true;
        }
    };
    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Ray Tracing");
    ImGui::Columns(2, nullptr, false);
    toggleFlag("Sun Light (Direct)", RenderFlags_LightBit);
    toggleFlag("RT Shadows", RenderFlags_ShadowBit);
    ImGui::NextColumn();
    toggleFlag("RT GI", RenderFlags_GIBit);
    toggleFlag("RT Reflections", RenderFlags_ReflectionBit);
    ImGui::Columns(1);
    toggleFlag("RT AO", RenderFlags_AOBit);
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "Post-Process");
    toggleFlag("SVGF Denoising", RenderFlags_SVGFBit);
    if (m_RenderFlags & RenderFlags_SVGFBit)
    {
        ImGui::Indent();
        toggleFlag("Temporal", RenderFlags_SVGFTemporalBit);
        toggleFlag("Spatial", RenderFlags_SVGFSpatialBit);
        ImGui::Unindent();
    }
    toggleFlag("TAA", RenderFlags_TAABit);
    ImGui::Spacing();
    toggleFlag("IBL Lighting", RenderFlags_IBLBit);
    toggleFlag("Emissive", RenderFlags_EmissiveBit);
    if (changed && activePath) activePath->OnSceneUpdated();
}

void EditorLayer::DrawControlPanelContent(RenderPath* activePath)
{
    if (ImGui::CollapsingHeader("Pipeline & View",
                                ImGuiTreeNodeFlags_DefaultOpen))
        DrawRenderPathPanel(activePath);
    ImGui::Spacing();
    DrawFeatureToggles(activePath);
    ImGui::Spacing();
    DrawLightSettings(activePath);
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Scene & Assets",
                                ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::TreeNodeEx("Hierarchy", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawSceneHierarchy();
            ImGui::TreePop();
        }

        DrawPropertiesPanel(activePath);

        if (ImGui::TreeNodeEx("Content Browser",
                              ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::BeginTabBar("ContentTabs"))
            {
                if (ImGui::BeginTabItem("Models"))
                {
                    ImGui::InputText("Search Models", m_AssetSearchFilter,
                                     IM_ARRAYSIZE(m_AssetSearchFilter));
                    std::string searchStr = m_AssetSearchFilter;
                    std::transform(searchStr.begin(), searchStr.end(),
                                   searchStr.begin(), ::tolower);

                    ImGui::BeginChild("ModelScroll", ImVec2(0, 150), true);
                    for (int i = 0; i < (int)m_AvailableModels.size(); i++)
                    {
                        std::string lowerName = m_AvailableModels[i].Name;
                        std::transform(lowerName.begin(), lowerName.end(),
                                       lowerName.begin(), ::tolower);

                        if (!searchStr.empty() &&
                            lowerName.find(searchStr) == std::string::npos)
                            continue;

                        ImGui::PushID(i);
                        if (ImGui::Selectable(m_AvailableModels[i].Name.c_str(),
                                              m_SelectedAssetIndex == i))
                        {
                            InvalidateBenchmarkScenePreset();
                            m_SelectedAssetIndex = i;
                            m_ActiveAssetPath = m_AvailableModels[i].Path;
                            ResourceManager::Get().LoadScene(m_ActiveAssetPath);
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Environments"))
                {
                    ImGui::InputText("Search HDRs", m_AssetSearchFilter,
                                     IM_ARRAYSIZE(m_AssetSearchFilter));
                    std::string searchStr = m_AssetSearchFilter;
                    std::transform(searchStr.begin(), searchStr.end(),
                                   searchStr.begin(), ::tolower);

                    ImGui::BeginChild("HDRScroll", ImVec2(0, 150), true);
                    for (int i = 0; i < (int)m_AvailableHDRs.size(); i++)
                    {
                        std::string lowerName = m_AvailableHDRs[i].Name;
                        std::transform(lowerName.begin(), lowerName.end(),
                                       lowerName.begin(), ::tolower);

                        if (!searchStr.empty() &&
                            lowerName.find(searchStr) == std::string::npos)
                            continue;

                        ImGui::PushID(i);
                        if (ImGui::Selectable(m_AvailableHDRs[i].Name.c_str(),
                                              m_SelectedAssetIndex == i))
                        {
                            InvalidateBenchmarkScenePreset();
                            m_SelectedAssetIndex = i;
                            m_ActiveAssetPath = m_AvailableHDRs[i].Path;
                            ResourceManager::Get().LoadHDR(m_ActiveAssetPath);
                            if (activePath) activePath->OnSceneUpdated();
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::TreePop();
        }
    }
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Debug Information",
                                ImGuiTreeNodeFlags_DefaultOpen))
    {
        // 1. Frame Statistics & Performance Graph
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "Frame Time: %.3f ms",
                           m_AverageFrameTime);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "(%.1f FPS)", m_AverageFPS);

        // Simple performance plot
        static float values[100] = {};
        static int values_offset = 0;
        values[values_offset] = m_AverageFrameTime;
        values_offset = (values_offset + 1) % 100;
        ImGui::PlotLines("##FrameTime", values, 100, values_offset,
                         "Performance Analysis (ms)", 0.0f, 33.0f,
                         ImVec2(0, 50));

        const auto& stats = Application::Get().GetFrameStats();
        ImGui::Text("Render Stats: %u Meshes (%u Visible, %u Culled)",
                    stats.TotalMeshes, stats.DrawCalls, stats.CulledMeshes);

        ImGui::Spacing();
        ImGui::Separator();

        // 2. Camera Parameters
        if (ImGui::TreeNodeEx("Camera Parameters",
                              ImGuiTreeNodeFlags_DefaultOpen))
        {
            glm::vec3 pos = m_EditorCamera.GetPosition();
            float fov = m_EditorCamera.GetFOV();
            ImGui::Text("Position: [%.2f, %.2f, %.2f]", pos.x, pos.y, pos.z);
            ImGui::Text("FOV: %.1f", fov);
            if (ImGui::SmallButton("Reset Camera"))
            {
                m_EditorCamera.Reset();
                if (activePath)
                {
                    activePath->InvalidateHistory();
                }
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Frame Capture"))
        {
            auto& lastCapturePath = m_LastCapturePath;
            auto& lastDifferencePath = m_LastDifferencePath;
            auto& captureStatus = m_CaptureStatus;
            auto& captureSucceeded = m_CaptureSucceeded;
            auto& captureStatusIsError = m_CaptureStatusIsError;
            auto& pendingAction = m_PendingCaptureAction;
            auto& channelThreshold = m_ChannelThreshold;
            auto& allowedDifferentPixels = m_AllowedDifferentPixels;
            auto& allowedMaxChannelDifference =
                m_AllowedMaxChannelDifference;
            auto& allowedRmse = m_AllowedRmse;
            auto& differenceAmplification = m_DifferenceAmplification;
            auto& pendingSignature = m_PendingRegressionSignature;

            const std::filesystem::path baselinePath =
                MakeRegressionBaselinePath();
            const std::filesystem::path signaturePath =
                MakeRegressionSignaturePath();
            const bool renderConfigurationReady =
                activePath && activePath->HasRenderGraph();
            const std::string currentSignature =
                renderConfigurationReady
                    ? MakeRegressionSignature(
                          *activePath, m_EditorCamera, m_RenderFlags,
                          m_DisplayMode, m_Exposure, m_AmbientStrength,
                          m_ClearColor, m_LightRadius, m_ActiveAssetPath,
                          static_cast<uint32_t>(
                              std::max(m_CaptureWarmupFrames, 1)),
                          GetActiveSceneRaw())
                    : std::string{};

            if (pendingAction != FrameCaptureAction::None &&
                !lastCapturePath.empty())
            {
                std::error_code fileError;
                if (std::filesystem::exists(lastCapturePath, fileError))
                {
                    captureSucceeded = true;
                    captureStatusIsError = false;

                    if (pendingAction == FrameCaptureAction::Baseline &&
                        pendingSignature != currentSignature)
                    {
                        captureStatusIsError = true;
                        captureStatus =
                            "Baseline rejected: render configuration changed "
                            "during warm-up. Capture again.";
                    }
                    else if (pendingAction == FrameCaptureAction::Baseline)
                    {
                        std::filesystem::copy_file(
                            lastCapturePath, baselinePath,
                            std::filesystem::copy_options::overwrite_existing,
                            fileError);
                        if (!fileError)
                        {
                            std::filesystem::remove(lastCapturePath, fileError);
                            std::string signatureError;
                            if (WriteImageRegressionSignature(
                                    signaturePath.string(), pendingSignature,
                                    signatureError))
                            {
                                lastCapturePath = baselinePath;
                                captureStatus =
                                    "Regression baseline updated: " +
                                    baselinePath.string();
                            }
                            else
                            {
                                captureSucceeded = false;
                                captureStatusIsError = true;
                                captureStatus = "Baseline metadata failed: " +
                                                signatureError;
                            }
                        }
                        else
                        {
                            captureSucceeded = false;
                            captureStatusIsError = true;
                            captureStatus = "Baseline update failed: " +
                                            fileError.message();
                        }
                    }
                    else if (pendingAction ==
                                 FrameCaptureAction::Regression &&
                             pendingSignature != currentSignature)
                    {
                        captureStatusIsError = true;
                        captureStatus =
                            "Regression blocked: render configuration changed "
                            "while capture was pending. Capture again.";
                        lastDifferencePath.clear();
                    }
                    else if (pendingAction ==
                             FrameCaptureAction::Regression)
                    {
                        ImageRegressionSettings settings;
                        settings.channelThreshold = static_cast<uint8_t>(
                            std::clamp(channelThreshold, 0, 255));
                        settings.allowedDifferentPixelCount =
                            static_cast<uint64_t>(
                                std::max(allowedDifferentPixels, 0));
                        settings.allowedMaxChannelDifference =
                            static_cast<uint8_t>(std::clamp(
                                allowedMaxChannelDifference, 0, 255));
                        settings.allowedRmse =
                            std::max(static_cast<double>(allowedRmse), 0.0);
                        settings.differenceAmplification =
                            static_cast<uint8_t>(std::clamp(
                                differenceAmplification, 1, 255));

                        lastDifferencePath =
                            MakeDifferencePath(lastCapturePath);
                        const ImageRegressionResult regressionResult =
                            RunImageRegression(
                            baselinePath.string(), lastCapturePath.string(),
                            lastDifferencePath.string(), settings);

                        std::ostringstream status;
                        if (!regressionResult.success)
                        {
                            captureStatusIsError = true;
                            captureStatus = "Regression error: " +
                                            regressionResult.error;
                            lastDifferencePath.clear();
                        }
                        else
                        {
                            captureStatusIsError =
                                !regressionResult.passed;
                            status << (regressionResult.passed
                                           ? "Regression PASS"
                                           : "Regression FAIL")
                                   << " | different pixels: "
                                   << regressionResult.comparison
                                          .differentPixelCount
                                   << " | max channel: "
                                   << static_cast<uint32_t>(
                                          regressionResult.comparison
                                              .maxChannelDifference)
                                   << " | RMSE: " << std::fixed
                                   << std::setprecision(3)
                                   << regressionResult.comparison.rmse;
                            captureStatus = status.str();
                            if (regressionResult.passed)
                            {
                                lastDifferencePath.clear();
                            }
                        }
                    }
                    else
                    {
                        captureStatus =
                            "Saved to: " + lastCapturePath.string();
                    }

                    pendingAction = FrameCaptureAction::None;
                    pendingSignature.clear();
                }
            }

            Renderer& renderer = Renderer::Get();
            const bool captureBusy = renderer.HasFrameCaptureRequest() ||
                                     pendingAction != FrameCaptureAction::None ||
                                     m_WarmupCaptureAction !=
                                         FrameCaptureAction::None;
            const bool baselineExists =
                std::filesystem::exists(baselinePath) &&
                std::filesystem::exists(signaturePath);

            ImGui::BeginDisabled(captureBusy || !renderConfigurationReady);
            if (ImGui::Button("Capture Frame"))
            {
                const std::filesystem::path capturePath =
                    MakeFrameCapturePath();
                if (renderer.RequestFrameCapture(capturePath))
                {
                    lastCapturePath = capturePath;
                    lastDifferencePath.clear();
                    captureSucceeded = false;
                    captureStatusIsError = false;
                    captureStatus = "Capture requested...";
                    pendingAction = FrameCaptureAction::Capture;
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Set Regression Baseline"))
            {
                const std::filesystem::path candidatePath =
                    MakeFrameCapturePath();
                activePath->InvalidateHistory();
                m_EditorCamera.ResetTemporalHistory();
                m_CaptureTemporalFrameIndex = 0;
                pendingSignature = currentSignature;
                m_WarmupCapturePath = candidatePath;
                m_WarmupCaptureAction = FrameCaptureAction::Baseline;
                m_CaptureWarmup.Start(static_cast<uint32_t>(
                    std::max(m_CaptureWarmupFrames, 1)));
                lastDifferencePath.clear();
                captureSucceeded = false;
                captureStatusIsError = false;
                captureStatus = "Warming temporal history...";
            }
            ImGui::EndDisabled();

            ImGui::TextDisabled("Baseline: %s",
                                baselineExists ? baselinePath.string().c_str()
                                               : "Not created");
            ImGui::InputInt("Warm-up frames", &m_CaptureWarmupFrames);
            ImGui::InputInt("Per-channel tolerance", &channelThreshold);
            ImGui::InputInt("Allowed different pixels",
                            &allowedDifferentPixels);
            ImGui::InputInt("Allowed max channel difference",
                            &allowedMaxChannelDifference);
            ImGui::InputFloat("Allowed RMSE", &allowedRmse, 0.1f, 1.0f,
                              "%.3f");
            ImGui::InputInt("Difference amplification",
                            &differenceAmplification);

            ImGui::BeginDisabled(captureBusy || !baselineExists ||
                                 !renderConfigurationReady);
            if (ImGui::Button("Capture and Compare"))
            {
                const ImageRegressionSignatureResult signatureResult =
                    ValidateImageRegressionSignature(signaturePath.string(),
                                                     currentSignature);
                if (!signatureResult.success || !signatureResult.matches)
                {
                    captureSucceeded = false;
                    captureStatusIsError = true;
                    captureStatus = "Regression blocked: " +
                                    signatureResult.error +
                                    ". Capture a new baseline.";
                }
                else
                {
                    const std::filesystem::path actualPath =
                        MakeFrameCapturePath();
                    activePath->InvalidateHistory();
                    m_EditorCamera.ResetTemporalHistory();
                    m_CaptureTemporalFrameIndex = 0;
                    pendingSignature = currentSignature;
                    m_WarmupCapturePath = actualPath;
                    m_WarmupCaptureAction =
                        FrameCaptureAction::Regression;
                    m_CaptureWarmup.Start(static_cast<uint32_t>(
                        std::max(m_CaptureWarmupFrames, 1)));
                    lastDifferencePath.clear();
                    captureSucceeded = false;
                    captureStatusIsError = false;
                    captureStatus = "Warming temporal history...";
                }
            }
            ImGui::EndDisabled();

            if (captureBusy)
            {
                if (m_WarmupCaptureAction != FrameCaptureAction::None)
                {
                    ImGui::TextDisabled("Warm-up frames remaining: %u",
                                        m_CaptureWarmup.GetRemainingFrames());
                }
                else
                {
                    ImGui::TextDisabled(
                        "Waiting for GPU capture and CPU PNG write...");
                }
            }

            if (!captureStatus.empty())
            {
                const ImVec4 statusColor =
                    captureStatusIsError
                        ? ImVec4(1, 0.3f, 0.3f, 1)
                        : captureSucceeded ? ImVec4(0, 1, 0, 1)
                                           : ImVec4(1, 0.8f, 0, 1);
                ImGui::TextColored(statusColor, "%s", captureStatus.c_str());
            }

            if (captureSucceeded && !lastCapturePath.empty() &&
                ImGui::Button("Open Capture"))
            {
                ShellExecuteW(nullptr, L"open", lastCapturePath.c_str(),
                              nullptr, nullptr, SW_SHOWNORMAL);
            }

            if (!lastDifferencePath.empty())
            {
                ImGui::SameLine();
                if (ImGui::Button("Open Difference"))
                {
                    ShellExecuteW(nullptr, L"open",
                                  lastDifferencePath.c_str(), nullptr, nullptr,
                                  SW_SHOWNORMAL);
                }
            }

            ImGui::TreePop();
        }

        if (ImGui::TreeNode("GPU Benchmark"))
        {
            static std::string exportStatus;
            static bool exportSucceeded = false;
            static std::filesystem::path lastExportPath;

            if (activePath && activePath->HasRenderGraph())
            {
                const auto& benchmark = activePath->GetBenchmarkRecorder();
                const bool sceneLoading =
                    ResourceManager::Get().HasPendingModelLoads();

                if (!benchmark.IsRunning())
                {
                    const bool prepareBlocked =
                        sceneLoading ||
                        m_BenchmarkSceneState ==
                            BenchmarkSceneState::Preparing;
                    ImGui::BeginDisabled(prepareBlocked);
                    if (ImGui::Button("Prepare Benchmark Scene"))
                    {
                        PrepareBenchmarkScene();
                        exportStatus.clear();
                        lastExportPath.clear();
                    }
                    ImGui::EndDisabled();

                    switch (m_BenchmarkSceneState)
                    {
                        case BenchmarkSceneState::Unprepared:
                            ImGui::TextDisabled("Scene preset: Unprepared");
                            break;
                        case BenchmarkSceneState::Preparing:
                            ImGui::TextColored(ImVec4(1, 0.8f, 0, 1),
                                               "Scene preset: Preparing...");
                            break;
                        case BenchmarkSceneState::Ready:
                            ImGui::TextColored(ImVec4(0, 1, 0, 1),
                                               "Scene preset: Ready");
                            break;
                        case BenchmarkSceneState::Failed:
                            ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1),
                                               "Scene preset: Failed");
                            break;
                    }

                    const bool canStart =
                        !sceneLoading &&
                        m_BenchmarkSceneState == BenchmarkSceneState::Ready;
                    ImGui::BeginDisabled(!canStart);
                    if (ImGui::Button(benchmark.IsComplete()
                                          ? "Run Benchmark Again"
                                          : "Start Benchmark"))
                    {
                        ApplyBenchmarkCameraPreset(m_EditorCamera);
                        activePath->StartBenchmark(120, 300);
                        exportStatus.clear();
                        lastExportPath.clear();
                    }
                    ImGui::EndDisabled();

                    if (sceneLoading)
                    {
                        ImGui::TextDisabled(
                            "Wait for asynchronous model loading to finish.");
                    }
                }
                else
                {
                    ImGui::Text("Warmup remaining: %u",
                                benchmark.GetWarmupFramesRemaining());

                    ImGui::Text("Captured frames: %u / 300",
                                benchmark.GetCapturedFrameCount());
                    ImGui::TextDisabled("Benchmark camera is locked.");

                    if (ImGui::Button("Cancel Benchmark"))
                    {
                        activePath->ResetBenchmark();
                        exportStatus.clear();
                        lastExportPath.clear();
                    }
                }

                if (benchmark.IsComplete())
                {
                    ImGui::TextColored(ImVec4(0, 1, 0, 1),
                                       "Benchmark Complete");

                    std::vector<const PassTimingStatistics*>
                        sortedStatistics;
                    sortedStatistics.reserve(
                        benchmark.GetStatistics().size());

                    for (const auto& [name, statistics] :
                         benchmark.GetStatistics())
                    {
                        sortedStatistics.push_back(&statistics);
                    }

                    std::sort(
                        sortedStatistics.begin(), sortedStatistics.end(),
                        [](const PassTimingStatistics* lhs,
                           const PassTimingStatistics* rhs)
                        {
                            return lhs->name < rhs->name;
                        });

                    if (ImGui::BeginTable(
                            "BenchmarkResults", 4,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_SizingStretchProp))
                    {
                        ImGui::TableSetupColumn("Pass");
                        ImGui::TableSetupColumn("Avg ms");
                        ImGui::TableSetupColumn("P50 ms");
                        ImGui::TableSetupColumn("P95 ms");
                        ImGui::TableHeadersRow();

                        for (const PassTimingStatistics* statistics :
                             sortedStatistics)
                        {
                            ImGui::TableNextRow();

                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted(statistics->name.c_str());

                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%.3f",
                                        statistics->GetAverageMS());

                            ImGui::TableSetColumnIndex(2);
                            ImGui::Text("%.3f", statistics->GetP50MS());

                            ImGui::TableSetColumnIndex(3);
                            ImGui::Text("%.3f", statistics->GetP95MS());
                        }

                        ImGui::EndTable();
                    }

                    if (ImGui::Button("Export CSV"))
                    {
                        BenchmarkCsvMetadata metadata;
                        metadata.gpuName =
                            Application::Get()
                                .GetContext()
                                ->GetDeviceProperties()
                                .deviceName;
                        metadata.renderPath =
                            RenderPathTypeToString(activePath->GetType());
                        metadata.scenePreset = BenchmarkScenePresetName;
                        metadata.width =
                            activePath->GetRenderGraph().GetWidth();
                        metadata.height =
                            activePath->GetRenderGraph().GetHeight();
                        metadata.renderFlags =
                            static_cast<uint32_t>(m_RenderFlags);

                        const BenchmarkCsvResult result = WriteBenchmarkCsv(
                            benchmark, metadata, MakeBenchmarkCsvPath());
                        exportSucceeded = result.success;
                        lastExportPath =
                            result.success ? result.path
                                           : std::filesystem::path{};
                        exportStatus = result.success
                                           ? "Saved to: " + result.path.string()
                                           : "Export failed: " + result.error;
                    }

                    if (!exportStatus.empty())
                    {
                        const ImVec4 statusColor =
                            exportSucceeded ? ImVec4(0, 1, 0, 1)
                                            : ImVec4(1, 0.3f, 0.3f, 1);
                        ImGui::TextColored(statusColor, "%s",
                                           exportStatus.c_str());
                    }

                    if (!lastExportPath.empty() && ImGui::Button("Open CSV"))
                    {
                        if (!std::filesystem::exists(lastExportPath))
                        {
                            exportSucceeded = false;
                            exportStatus = "Open failed: exported file no "
                                           "longer exists";
                            lastExportPath.clear();
                        }
                        else
                        {
                            const HINSTANCE openResult = ShellExecuteW(
                                nullptr, L"open",
                                lastExportPath.c_str(), nullptr, nullptr,
                                SW_SHOWNORMAL);

                            if (reinterpret_cast<intptr_t>(openResult) <= 32)
                            {
                                exportSucceeded = false;
                                exportStatus = "Open failed: Windows could not "
                                               "find an application for CSV";
                            }
                        }
                    }
                }
            }
            else
            {
                ImGui::TextDisabled("RenderGraph is not ready.");
            }

            ImGui::TreePop();
        }

        // 3. GPU Pass Breakdown
        if (ImGui::TreeNode("GPU Pass Breakdown"))
        {
            if (activePath)
                activePath->GetRenderGraph().DrawPerformanceStatistics();
            ImGui::TreePop();
        }

        // 4. Export RenderGraph
        if (ImGui::Button("Export Render Graph (Mermaid)", ImVec2(-1, 0)) &&
            activePath)
        {
            ImGui::SetClipboardText(
                activePath->GetRenderGraph().ExportToMermaid().c_str());
        }
    }

    if (activePath)
    {
        ImGui::Spacing();
        ImGui::Separator();
        activePath->OnImGui();
    }
}
} // namespace Chimera
