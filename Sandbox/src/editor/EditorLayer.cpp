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
#include <imgui.h>
#include <imgui_internal.h>
#include <filesystem>
#include "Renderer/Pipelines/RenderPathFactory.h"
#include "Renderer/RenderState.h"

namespace Chimera
{

    EditorLayer::EditorLayer()
        : Layer("EditorLayer"),
          m_EditorCamera(45.0f, 1.778f, 0.1f, 1000.0f)
    {
        m_ShowControlPanel = true;

        auto &app = Application::Get();
        m_ViewportSize = {(float)app.GetWindow().GetWidth(), (float)app.GetWindow().GetHeight()};

        m_EditorCamera.SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
        m_EditorCamera.SetFocalPoint({0.0f, 1.0f, 0.0f});
        m_EditorCamera.SetDistance(5.0f);

        m_RenderFlags = RENDER_FLAG_SHADOW_BIT;

        auto scene = std::make_shared<Scene>(app.GetContext());
        ResourceManager::Get().SetActiveScene(scene);

        SwitchRenderPath(RenderPathType::Hybrid);
    }

    void EditorLayer::OnAttach()
    {
        RefreshModelList();
        LoadScene(Application::Get().GetSpecification().AssetDir + "models/Sponza/scene.gltf");
    }

    void EditorLayer::OnDetach()
    {
    }

    void EditorLayer::OnUpdate(Timestep ts)
    {
        m_AverageFrameTime = ts.GetMilliseconds();
        m_AverageFPS = 1.0f / ts.GetSeconds();

        auto& window = Application::Get().GetWindow();
        float winW = (float)window.GetWidth();
        float winH = (float)window.GetHeight();

        if (winW > 0 && (std::abs(winW - m_ViewportSize.x) > 0.1f || std::abs(winH - m_ViewportSize.y) > 0.1f))
        {
            // Immediate resize for background rendering to stay sharp
            vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
            Renderer::Get().OnResize((uint32_t)winW, (uint32_t)winH);
            if (GetRenderPath())
            {
                GetRenderPath()->SetViewportSize((uint32_t)winW, (uint32_t)winH);
            }
            m_EditorCamera.SetViewportSize(winW, winH);
            m_ViewportSize = {winW, winH};
        }

        // [MOD] Camera interaction now works when not hovering over UI panels
        bool uiHovered = ImGui::GetIO().WantCaptureMouse;
        m_EditorCamera.OnUpdate(ts, !uiHovered, !uiHovered);
        m_EditorCamera.UpdateTAAState(Application::Get().GetTotalFrameCount(), (m_RenderFlags & RENDER_FLAG_TAA_BIT) != 0);

        if (auto scene = GetActiveSceneRaw())
        {
            scene->OnUpdate(ts.GetSeconds());
        }

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

        RenderPath *activePath = GetRenderPath();
        if (activePath && Renderer::Get().IsFrameInProgress())
        {
            RenderFrameInfo frameInfo{};
            frameInfo.commandBuffer = Renderer::Get().GetActiveCommandBuffer();
            frameInfo.frameIndex = Renderer::Get().GetCurrentFrameIndex();
            frameInfo.imageIndex = Renderer::Get().GetCurrentImageIndex();
            frameInfo.globalSet = Application::Get().GetRenderState()->GetDescriptorSet(frameInfo.frameIndex);
            activePath->Render(frameInfo);
        }
    }

    void EditorLayer::OnEvent(Event &e)
    {
        if (!ImGui::GetIO().WantCaptureMouse)
            m_EditorCamera.OnEvent(e);
    }

    void EditorLayer::SwitchRenderPath(RenderPathType type)
    {
        Application::Get().QueueEvent([this, type]()
                                      {
            CH_CORE_INFO("EditorLayer: Switching RenderPath to {}...", RenderPathTypeToString(type));
            vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
            try
            {
                auto newPath = RenderPathFactory::Create(type, Application::Get().GetContext());
                if (newPath)
                {
                    newPath->SetViewportSize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
                    Application::Get().SwitchRenderPath(std::move(newPath));
                }
            }
            catch (const std::exception& e)
            {
                CH_CORE_ERROR("EditorLayer: Failed to switch: {}", e.what());
            } });
    }

    void EditorLayer::LoadScene(const std::string &path)
    {
        Application::Get().QueueEvent([this, path]()
                                      {
            vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
            GetActiveSceneRaw()->LoadModel(path);
            if (GetRenderPath())
            {
                GetRenderPath()->OnSceneUpdated();
            } });
    }

    void EditorLayer::ClearScene()
    {
        Application::Get().QueueEvent([this]()
                                      {
            vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
            ResourceManager::Get().SetActiveScene(std::make_shared<Scene>(Application::Get().GetContext())); });
    }

    void EditorLayer::OnImGuiRender()
    {
        ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;

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
            ImGuiID dock_right_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.20f, nullptr, &dock_main_id);

            // The central node will be empty (passthru) to show the background
            ImGui::DockBuilderDockWindow("Control Panel", dock_right_id);
            ImGui::DockBuilderFinish(dockspace_id);
        }
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

        DrawMenuBar();

        RenderPath *activePath = GetRenderPath();

        ImGui::Begin("Control Panel", &m_ShowControlPanel);
        DrawControlPanelContent(activePath);
        ImGui::End();

        ImGui::End();
    }

    void EditorLayer::DrawSceneHierarchy()
    {
        Scene *scene = GetActiveSceneRaw();
        if (!scene)
            return;
        if (ImGui::Button("Clear Scene"))
        {
            ClearScene();
            m_SelectedInstanceIndex = -1;
        }
        ImGui::Separator();
        const auto &entities = scene->GetEntities();
        for (int i = 0; i < (int)entities.size(); i++)
        {
            ImGui::PushID(i);
            if (ImGui::Selectable((entities[i].name + "##" + std::to_string(i)).c_str(), m_SelectedInstanceIndex == i))
                m_SelectedInstanceIndex = i;
            ImGui::PopID();
        }
    }

    void EditorLayer::DrawPropertiesPanel(RenderPath *activePath)
    {
        Scene *scene = GetActiveSceneRaw();
        if (!scene || m_SelectedInstanceIndex < 0)
        {
            ImGui::Text("Select an object.");
            return;
        }

        const auto &entities = scene->GetEntities();
        auto &entity = entities[m_SelectedInstanceIndex];
        ImGui::Text("Name: %s", entity.name.c_str());

        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            bool changed = false;
            glm::vec3 pos = entity.transform.position;
            glm::vec3 rot = entity.transform.rotation;
            glm::vec3 scale = entity.transform.scale;

            if (ImGui::DragFloat3("Position", &pos.x, 0.1f))
                changed = true;
            if (ImGui::DragFloat3("Rotation", &rot.x, 0.5f))
                changed = true;
            if (ImGui::DragFloat3("Scale", &scale.x, 0.05f))
                changed = true;

            if (changed)
            {
                scene->UpdateEntityTRS(m_SelectedInstanceIndex, pos, rot, scale);
                if (activePath)
                    activePath->OnSceneUpdated();
            }
        }

        if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (entity.mesh.model)
            {
                uint32_t materialIndex = entity.mesh.model->GetMeshes()[0].materialIndex; // Assume 1st mesh for now
                Material *mat = ResourceManager::Get().GetMaterial(MaterialHandle(materialIndex));

                if (mat)
                {
                    GpuMaterial data = mat->GetData();
                    bool matChanged = false;

                    if (ImGui::ColorEdit3("Albedo", &data.albedo.x))
                        matChanged = true;
                    if (ImGui::SliderFloat("Roughness", &data.roughness, 0.0f, 1.0f))
                        matChanged = true;
                    if (ImGui::SliderFloat("Metallic", &data.metallic, 0.0f, 1.0f))
                        matChanged = true;
                    if (ImGui::SliderFloat("Emission Intensity", &data.emission.x, 0.0f, 10.0f))
                        matChanged = true;

                    if (matChanged)
                    {
                        ResourceManager::Get().UpdateMaterial(materialIndex, data);
                        scene->MarkMaterialDirty();
                        if (activePath)
                            activePath->OnSceneUpdated();
                    }
                }
            }
        }
    }

    void EditorLayer::RefreshModelList()
    {
        m_AvailableModels.clear();
        std::string rootPath = Application::Get().GetSpecification().AssetDir + "models";
        if (!std::filesystem::exists(rootPath))
        {
            return;
        }
        for (const auto &entry : std::filesystem::recursive_directory_iterator(rootPath))
        {
            if (entry.is_regular_file() && (entry.path().extension() == ".gltf" || entry.path().extension() == ".glb"))
            {
                m_AvailableModels.push_back({entry.path().filename().string(), std::filesystem::relative(entry.path(), ".").generic_string()});
            }
        }
    }

    void EditorLayer::LoadModel(const std::string &path)
    {
        m_ActiveModelPath = path;
        LoadScene(path);
    }

    void EditorLayer::DrawMenuBar()
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Refresh Assets"))
                    RefreshModelList();
                if (ImGui::MenuItem("Exit"))
                    Application::Get().Close();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View"))
            {
                ImGui::MenuItem("Control Panel", nullptr, &m_ShowControlPanel);
                ImGui::Separator();
                if (ImGui::MenuItem("Reset Layout"))
                    ImGui::GetIO().IniFilename = nullptr;
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    }

    void EditorLayer::DrawLightSettings(RenderPath *activePath)
    {
        Scene *scene = GetActiveSceneRaw();
        if (!scene)
            return;
        auto &light = scene->GetMainLight();
        bool changed = false;

        if (ImGui::TreeNodeEx("Environment (Skybox)", ImGuiTreeNodeFlags_DefaultOpen))
        {
            static char hdrPath[256] = "assets\\textures\\newport_loft.hdr";
            ImGui::InputText("HDR Path", hdrPath, sizeof(hdrPath));

            if (ImGui::Button("Load HDR Skybox"))
            {
                std::string path = hdrPath;
                if (std::filesystem::exists(path))
                {
                    scene->LoadHDRSkybox(path);
                    changed = true;
                }
                else
                {
                    CH_CORE_WARN("EditorLayer: HDR not found at {}.", path);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear Skybox"))
            {
                scene->ClearSkybox();
                changed = true;
            }

            ImGui::Text("Ambient Strength:");
            if (ImGui::SliderFloat("##AmbientStrength", &m_AmbientStrength, 0.0f, 10.0f))
                changed = true;

            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Directional Light", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::DragFloat3("Direction", &light.direction.x, 0.01f, -1.0f, 1.0f))
            {
                light.direction = glm::vec4(glm::normalize(glm::vec3(light.direction)), light.direction.w);
                changed = true;
            }
            if (ImGui::ColorEdit3("Color", &light.color.x))
                changed = true;

            float intensity = light.color.a;
            if (ImGui::DragFloat("Intensity", &intensity, 0.1f, 0.0f, 100.0f))
            {
                light.color.a = intensity;
                changed = true;
            }

            if (activePath && activePath->GetType() != RenderPathType::Forward)
            {
                if (ImGui::SliderFloat("Light Radius", &m_LightRadius, 0.0f, 0.5f))
                {
                    light.direction.w = m_LightRadius;
                    changed = true;
                }
            }
            ImGui::TreePop();
        }
        if (changed && activePath)
            activePath->OnSceneUpdated();
    }

    void EditorLayer::DrawRenderPathPanel(RenderPath *activePath)
    {
        const auto &allTypes = GetAllRenderPathTypes();
        RenderPathType currentType = activePath ? activePath->GetType() : RenderPathType::Forward;

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.8f, 1.0f, 1.0f));
        ImGui::Text("Core Pipeline");
        ImGui::PopStyleColor();
        ImGui::Separator();

        // 1. Path Selection
        if (ImGui::BeginCombo("Active Path", RenderPathTypeToString(currentType)))
        {
            for (auto type : allTypes)
            {
                if (ImGui::Selectable(RenderPathTypeToString(type), currentType == type))
                    SwitchRenderPath(type);
            }
            ImGui::EndCombo();
        }

        // 2. Display Mode Selection (THE most used tool)
        const char *displayModes[] =
            {
                "Final Color", "Albedo", "Normal", "Material", "Motion", "Depth",
                "Shadow/AO", "Reflection", "Diffuse GI", "Emissive", "SVGF Variance"};
        int currentDisplayMode = (int)m_DisplayMode;
        if (ImGui::Combo("Display Mode", &currentDisplayMode, displayModes, IM_ARRAYSIZE(displayModes)))
        {
            m_DisplayMode = (uint32_t)currentDisplayMode;
        }

        ImGui::SliderFloat("Exposure", &m_Exposure, 0.01f, 5.0f);

        if (currentType == RenderPathType::Forward)
        {
            ImGui::Spacing();
            bool taa = (m_RenderFlags & RENDER_FLAG_TAA_BIT) != 0;
            if (ImGui::Checkbox("Enable TAA (Anti-Aliasing)", &taa))
            {
                if (taa)
                {
                    m_RenderFlags |= RENDER_FLAG_TAA_BIT;
                }
                else
                {
                    m_RenderFlags &= ~RENDER_FLAG_TAA_BIT;
                }
                if (activePath)
                {
                    activePath->OnSceneUpdated();
                }
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Smooths geometric edges using temporal accumulation.");
            }
        }

        ImGui::Spacing();

        // 3. Modular Feature Toggles (Hybrid Only now for better clarity)
        if (currentType == RenderPathType::Hybrid)
        {
            if (ImGui::CollapsingHeader("Ray Tracing (Real-time)", ImGuiTreeNodeFlags_DefaultOpen))
            {
                bool shadow = (m_RenderFlags & RENDER_FLAG_SHADOW_BIT) != 0;
                if (ImGui::Checkbox("Hard/Soft Shadows", &shadow))
                {
                    if (shadow)
                        m_RenderFlags |= RENDER_FLAG_SHADOW_BIT;
                    else
                        m_RenderFlags &= ~RENDER_FLAG_SHADOW_BIT;
                }

                bool refl = (m_RenderFlags & RENDER_FLAG_REFLECTION_BIT) != 0;
                if (ImGui::Checkbox("Specular Reflections", &refl))
                {
                    if (refl)
                        m_RenderFlags |= RENDER_FLAG_REFLECTION_BIT;
                    else
                        m_RenderFlags &= ~RENDER_FLAG_REFLECTION_BIT;
                }

                bool gi = (m_RenderFlags & RENDER_FLAG_GI_BIT) != 0;
                if (ImGui::Checkbox("Diffuse GI (1-Bounce)", &gi))
                {
                    if (gi)
                        m_RenderFlags |= RENDER_FLAG_GI_BIT;
                    else
                        m_RenderFlags &= ~RENDER_FLAG_GI_BIT;
                }
            }

            if (ImGui::CollapsingHeader("Denoising & Stability", ImGuiTreeNodeFlags_DefaultOpen))
            {
                bool taa = (m_RenderFlags & RENDER_FLAG_TAA_BIT) != 0;
                if (ImGui::Checkbox("Temporal Anti-Aliasing", &taa))
                {
                    if (taa)
                        m_RenderFlags |= RENDER_FLAG_TAA_BIT;
                    else
                        m_RenderFlags &= ~RENDER_FLAG_TAA_BIT;
                    if (activePath)
                        activePath->OnSceneUpdated();
                }

                ImGui::Spacing();
                bool svgf = (m_RenderFlags & RENDER_FLAG_SVGF_BIT) != 0;
                if (ImGui::Checkbox("SVGF Denoising Network", &svgf))
                {
                    if (svgf)
                        m_RenderFlags |= RENDER_FLAG_SVGF_BIT;
                    else
                        m_RenderFlags &= ~RENDER_FLAG_SVGF_BIT;
                    if (activePath)
                        activePath->OnSceneUpdated();
                }
            }
        }
        if (activePath)
            activePath->OnImGui();
    }

    void EditorLayer::DrawControlPanelContent(RenderPath *activePath)
    {
        // Section 1: Top Level Pipeline Control
        DrawRenderPathPanel(activePath);
        ImGui::Spacing();
        ImGui::Separator();

        // Section 2: Environment
        if (ImGui::CollapsingHeader("Environment & Lighting"))
        {
            DrawGeneralSettings();
            ImGui::Spacing();
            DrawLightSettings(activePath);
        }

        // Section 3: Scene Data
        if (ImGui::CollapsingHeader("Scene Hierarchy"))
        {
            DrawSceneHierarchy();
        }

        if (ImGui::CollapsingHeader("Object Properties", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawPropertiesPanel(activePath);
        }

        // Section 4: Assets
        if (ImGui::CollapsingHeader("Asset Library"))
        {
            if (ImGui::Button("Refresh"))
                RefreshModelList();
            ImGui::SameLine();
            ImGui::InputTextWithHint("##Search", "Search...", m_AssetSearchFilter, sizeof(m_AssetSearchFilter));

            ImGui::BeginChild("AssetScroll", ImVec2(0, 150), true);
            std::string filter = m_AssetSearchFilter;
            std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);
            for (int i = 0; i < (int)m_AvailableModels.size(); i++)
            {
                std::string name = m_AvailableModels[i].Name;
                std::string lowName = name;
                std::transform(lowName.begin(), lowName.end(), lowName.begin(), ::tolower);
                if (!filter.empty() && lowName.find(filter) == std::string::npos)
                    continue;
                if (ImGui::Selectable(name.c_str(), m_SelectedModelIndex == i))
                {
                    m_SelectedModelIndex = i;
                    LoadModel(m_AvailableModels[i].Path);
                }
            }
            ImGui::EndChild();
        }

        // Section 5: Stats (Fixed at bottom)
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Performance Metrics", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("GPU: %s", VulkanContext::Get().GetDeviceProperties().deviceName);
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Frame: %.3f ms (%.1f FPS)", m_AverageFrameTime, m_AverageFPS);

            // [NEW] Scene Culling Stats
            const auto &stats = Application::Get().GetFrameStats();
            ImGui::Separator();
            ImGui::Text("Scene Stats:");
            ImGui::Text("  Total Meshes: %u", stats.TotalMeshes);
            ImGui::Text("  Visible Meshes: %u", stats.DrawCalls);
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "  Culled Meshes: %u", stats.CulledMeshes);

            if (stats.TotalMeshes > 0)
            {
                float culledRatio = (float)stats.CulledMeshes / (float)stats.TotalMeshes;
                ImGui::ProgressBar(culledRatio, ImVec2(-1, 0), "Culling Efficiency");
            }

            if (ImGui::TreeNode("GPU Pass Breakdown"))
            {
                if (activePath)
                    activePath->GetRenderGraph().DrawPerformanceStatistics();
                ImGui::TreePop();
            }

            if (ImGui::Button("Copy Graph to Clipboard", ImVec2(-1, 0)) && activePath)
            {
                ImGui::SetClipboardText(activePath->GetRenderGraph().ExportToMermaid().c_str());
            }
        }
    }

    void EditorLayer::DrawGeneralSettings()
    {
        ImGui::Text("Global Background Color:");
        if (ImGui::ColorEdit4("Clear Color", &m_ClearColor.x))
        {
            if (GetRenderPath())
                GetRenderPath()->OnSceneUpdated();
        }
    }
}
