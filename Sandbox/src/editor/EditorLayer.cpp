#include "pch.h"
#include "EditorLayer.h"
#include "Core/Application.h"
#include "Core/ImGuiLayer.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/Backend/Renderer.h"
#include "Utils/VulkanBarrier.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Pipelines/RenderPath.h"
#include "Scene/Scene.h"
#include "Core/Input.h"
#include "Scene/Model.h"
#include "Assets/AssetImporter.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <filesystem>
#include <algorithm>
#include "Renderer/Pipelines/RenderPathFactory.h"
#include "Renderer/RenderState.h"

namespace Chimera
{

EditorLayer::EditorLayer()
    : Layer("EditorLayer"), m_EditorCamera(45.0f, 1.778f, 0.1f, 1000.0f)
{
    m_ShowControlPanel = true;

    auto& app = Application::Get();
    m_ViewportSize = {(float)app.GetWindow().GetWidth(),
                      (float)app.GetWindow().GetHeight()};

    m_EditorCamera.SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
    m_EditorCamera.SetFocalPoint({-5.944f, 1.950f, -1.602f});
    m_EditorCamera.SetDistance(12.426f);
    m_EditorCamera.SetPitch(-0.032f);
    m_EditorCamera.SetYaw(-1.396f);
    m_EditorCamera.SetFOV(45.0f);

    m_RenderFlags = RenderFlags_LightBit | RenderFlags_ShadowBit |
                    RenderFlags_SVGFTemporalBit | RenderFlags_SVGFSpatialBit |
                    RenderFlags_IBLBit;

    m_AmbientStrength = 0.0f;
    m_Exposure = 1.0f;
    m_LightRadius = 0.5f;

    auto scene = std::make_shared<Scene>(app.GetContext());

    // Setup initial light for Sponza (High-angle slanting down)
    auto& light = scene->GetMainLight();
    light.direction =
        glm::vec4(glm::normalize(glm::vec3(0.085f, -0.987f, 0.139f)),
                  0.5f); // direction.w is radius
    light.color = glm::vec4(1.0f, 0.95f, 0.8f, 5.0f); // RGB + Intensity (A)

    ResourceManager::Get().SetActiveScene(scene);

    Application::Get().SwitchRenderPath(RenderPathType::Hybrid);
}

void EditorLayer::OnAttach()
{
    RefreshAssetList();

    ResourceManager::Get().LoadScene(
        Application::Get().GetSpecification().AssetDir +
        "models/Sponza/glTF/Sponza.gltf");

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
    ResourceManager::Get().ClearScene();
    m_SelectedInstanceIndex = -1;
    m_SelectedAssetIndex = -1;
    m_ActiveAssetPath = "";
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
        vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
        Renderer::Get().OnResize((uint32_t)winW, (uint32_t)winH);
        if (GetRenderPath())
            GetRenderPath()->SetViewportSize((uint32_t)winW, (uint32_t)winH);
        m_EditorCamera.SetViewportSize(winW, winH);
        m_ViewportSize = {winW, winH};
    }

    bool uiHovered = ImGui::GetIO().WantCaptureMouse;
    m_EditorCamera.OnUpdate(ts, !uiHovered, !uiHovered);
    m_EditorCamera.UpdateTAAState(Application::Get().GetTotalFrameCount(),
                                  (m_RenderFlags & RenderFlags_TAABit) != 0);

    if (auto scene = GetActiveSceneRaw()) scene->OnUpdate(ts.GetSeconds());

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
    context.DeltaTime = ts.GetSeconds();
    context.Time = (float)glfwGetTime();
    context.FrameIndex = Application::Get().GetTotalFrameCount();
    context.RenderFlags = m_RenderFlags;
    context.Exposure = m_Exposure;
    context.DisplayMode = m_DisplayMode;
    context.AmbientStrength = m_AmbientStrength;
    context.ClearColor = m_ClearColor;
    context.LightRadius = m_LightRadius;

    Application::Get().SetFrameContext(context);
    Application::Get().SetActiveScene(GetActiveSceneRaw());

    RenderPath* activePath = GetRenderPath();
    if (activePath && Renderer::Get().IsFrameInProgress())
    {
        RenderFrameInfo frameInfo{};
        frameInfo.commandBuffer = Renderer::Get().GetActiveCommandBuffer();
        frameInfo.frameIndex = Renderer::Get().GetCurrentFrameIndex();
        frameInfo.imageIndex = Renderer::Get().GetCurrentImageIndex();
        frameInfo.globalSet =
            Application::Get().GetRenderState()->GetDescriptorSet(
                frameInfo.frameIndex);
        activePath->Render(frameInfo);
    }
}

void EditorLayer::OnEvent(Event& e)
{
    if (!ImGui::GetIO().WantCaptureMouse)
    {
        m_EditorCamera.OnEvent(e);
    }
}

void EditorLayer::OnImGuiRender()
{
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

    if (changed && activePath) activePath->OnSceneUpdated();

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
    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
    {
        glm::vec3 pos = m_EditorCamera.GetPosition();
        glm::vec3 focal = m_EditorCamera.GetFocalPoint();
        float distance = m_EditorCamera.GetDistance();
        float pitch = m_EditorCamera.GetPitch();
        float yaw = m_EditorCamera.GetYaw();
        float fov = m_EditorCamera.GetFOV();

        ImGui::Text("Position: %.3f, %.3f, %.3f", pos.x, pos.y, pos.z);
        ImGui::Text("Focal Point: %.3f, %.3f, %.3f", focal.x, focal.y, focal.z);
        ImGui::Text("Distance: %.3f", distance);
        ImGui::Text("Pitch: %.3f, Yaw: %.3f", pitch, yaw);
        ImGui::Text("FOV: %.3f", fov);

        if (ImGui::Button("Reset Camera"))
        {
            m_EditorCamera.Reset();
        }
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0, 1, 0, 1), "Performance: %.3f ms (%.1f FPS)",
                       m_AverageFrameTime, m_AverageFPS);

    const auto& stats = Application::Get().GetFrameStats();
    ImGui::Text("Scene: %u Meshes (%u Visible, %u Culled)", stats.TotalMeshes,
                stats.DrawCalls, stats.CulledMeshes);

    if (ImGui::TreeNode("GPU Pass Breakdown"))
    {
        if (activePath)
            activePath->GetRenderGraph().DrawPerformanceStatistics();
        ImGui::TreePop();
    }

    if (ImGui::Button("Copy RenderGraph (Mermaid)", ImVec2(-1, 0)) &&
        activePath)
    {
        ImGui::SetClipboardText(
            activePath->GetRenderGraph().ExportToMermaid().c_str());
    }

    if (activePath)
    {
        ImGui::Spacing();
        ImGui::Separator();
        activePath->OnImGui();
    }
}
} // namespace Chimera
