#include "pch.h"
#include "EditorLayer.h"
#include "Core/ImGuiLayer.h"
#include "Core/EngineConfig.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/Backend/Renderer.h"
#include "Utils/VulkanBarrier.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Pipelines/RenderPath.h"
#include "Scene/Scene.h"
#include "Core/Input.h"
#include <imgui.h>
#include <filesystem>

namespace Chimera {

    EditorLayer::EditorLayer(Application* app)
        : m_App(app), m_EditorCamera(45.0f, 1.778f, 0.1f, 1000.0f)
    {
        // Initial defaults to prevent invalid projection matrices
        m_ViewportSize = { 1600.0f, 900.0f };
        m_EditorCamera.SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
        
        // Initialize camera at a better position
        m_EditorCamera.SetFocalPoint({ 0.0f, 1.0f, 0.0f });
        m_EditorCamera.SetDistance(5.0f);
    }

    void EditorLayer::OnAttach()
    {
        RefreshModelList();
    }

    void EditorLayer::OnDetach()
    {
    }

    void EditorLayer::OnUpdate(Timestep ts)
    {
        m_AverageFrameTime = ts.GetMilliseconds();
        m_AverageFPS = 1.0f / ts.GetSeconds();

        m_EditorCamera.OnUpdate(ts, m_ViewportHovered, m_ViewportFocused);

        // Sync state to Application
        AppFrameContext context;
        context.View = m_EditorCamera.GetViewMatrix();
        context.Projection = m_EditorCamera.GetProjection();
        context.CameraPosition = m_EditorCamera.GetPosition();
        context.ViewportSize = m_ViewportSize;
        context.DeltaTime = ts.GetSeconds();
        context.Time = (float)glfwGetTime();
        context.FrameIndex = m_App->GetTotalFrameCount();
        m_App->SetFrameContext(context);
    }

    void EditorLayer::OnEvent(Event& e)
    {
        if (e.GetEventType() == EventType::MouseScrolled)
        {
            if (m_ViewportHovered)
                m_EditorCamera.OnEvent(e);
            else
                return; // Don't let scroll through if not over viewport
        }
        else
        {
            m_EditorCamera.OnEvent(e);
        }
    }

    void EditorLayer::OnUIRender()
    {
        // Setup Docking
        static bool dockingEnabled = true;
        if (dockingEnabled)
        {
            static bool dockspaceOpen = true;
            static bool opt_fullscreen_persistant = true;
            bool opt_fullscreen = opt_fullscreen_persistant;
            static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

            ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
            if (opt_fullscreen)
            {
                ImGuiViewport* viewport = ImGui::GetMainViewport();
                ImGui::SetNextWindowPos(viewport->Pos);
                ImGui::SetNextWindowSize(viewport->Size);
                ImGui::SetNextWindowViewport(viewport->ID);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
                window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
                window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
            }

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::Begin("DockSpace Demo", &dockspaceOpen, window_flags);
            ImGui::PopStyleVar();

            if (opt_fullscreen)
                ImGui::PopStyleVar(2);

            // DockSpace
            ImGuiIO& io = ImGui::GetIO();
            if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
            {
                ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
                ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
            }

            DrawMenuBar();
            
            // --- Content ---
            DrawViewport();

            ImGui::Begin("Settings");
            if (ImGui::BeginTabBar("SettingsTabs"))
            {
                if (ImGui::BeginTabItem("Renderer"))
                {
                    DrawRenderPathPanel();
                    ImGui::Separator();

                    ImGui::Text("Asset Selection");
                    DrawModelSelectionPanel();
                    ImGui::EndTabItem();

                    ImGui::Text("Scene Hierarchy");
                    DrawSceneHierarchy();
                    ImGui::Separator();
                    DrawPropertiesPanel();
                    ImGui::Separator();

                }
                if (ImGui::BeginTabItem("Stats"))
                {
                    DrawStatsPanel();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::End();

            ImGui::End(); // DockSpace Window
        }
    }

    void EditorLayer::DrawSceneHierarchy()
    {
        auto scene = m_App->GetScene();
        if (!scene) return;

        if (ImGui::Button("Clear Scene")) {
            m_App->ClearScene();
            m_SelectedInstanceIndex = -1;
        }

        ImGui::Separator();
        
        const auto& entities = scene->GetEntities();
        for (int i = 0; i < (int)entities.size(); i++)
        {
            ImGui::PushID(i);
            bool selected = (m_SelectedInstanceIndex == i);
            std::string label = entities[i].name + "##" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), selected))
            {
                m_SelectedInstanceIndex = i;
            }
            ImGui::PopID();
        }
    }

    void EditorLayer::DrawPropertiesPanel()
    {
        auto scene = m_App->GetScene();
        if (!scene || m_SelectedInstanceIndex < 0) {
            ImGui::Text("Select an object in the Scene panel to view properties.");
            return;
        }

        const auto& entities = scene->GetEntities();
        if (m_SelectedInstanceIndex >= (int)entities.size()) {
            m_SelectedInstanceIndex = -1;
            return;
        }

        auto& entity = entities[m_SelectedInstanceIndex];
        ImGui::Text("Name: %s", entity.name.c_str());
        ImGui::Separator();

        // --- Transform ---
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            bool changed = false;
            glm::vec3 pos = entity.transform.position;
            glm::vec3 rot = entity.transform.rotation;
            glm::vec3 scale = entity.transform.scale;

            if (ImGui::DragFloat3("Position", &pos.x, 0.1f)) changed = true;
            if (ImGui::DragFloat3("Rotation", &rot.x, 0.5f)) changed = true;
            if (ImGui::DragFloat3("Scale", &scale.x, 0.05f)) changed = true;

            if (ImGui::Button("Reset Transform")) {
                pos = {0,0,0}; rot = {0,0,0}; scale = {1,1,1};
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete Entity")) {
                scene->RemoveEntity(m_SelectedInstanceIndex);
                m_SelectedInstanceIndex = -1;
                return;
            }

            if (changed) {
                scene->UpdateEntityTRS(m_SelectedInstanceIndex, pos, rot, scale);
                if (m_App->GetCurrentRenderPathType() == RenderPathType::RayTracing)
                    m_App->GetRenderPath()->OnSceneUpdated();
            }
        }

        ImGui::Separator();

        // --- PBRMaterial ---
        if (ImGui::CollapsingHeader("PBRMaterial", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (entity.mesh.material.IsValid())
            {
                auto& materials = m_App->GetResourceManager()->GetMaterials();
                auto& matObj = materials[entity.mesh.material.Get().id];
                
                PBRMaterial mat = matObj->GetData();
                bool matChanged = false;

                if (ImGui::ColorEdit4("Albedo", glm::value_ptr(mat.albedo))) { matObj->SetAlbedo(mat.albedo); matChanged = true; }
                if (ImGui::SliderFloat("Metallic", &mat.metallic, 0.0f, 1.0f)) { matObj->SetMetallic(mat.metallic); matChanged = true; }
                if (ImGui::SliderFloat("Roughness", &mat.roughness, 0.0f, 1.0f)) { matObj->SetRoughness(mat.roughness); matChanged = true; }
                if (ImGui::ColorEdit3("Emission", glm::value_ptr(mat.emission))) { matObj->SetEmission(mat.emission); matChanged = true; }

                if (matChanged)
                {
                    m_App->GetResourceManager()->SyncMaterialsToGPU();
                    if (m_App->GetCurrentRenderPathType() == RenderPathType::RayTracing)
                        m_App->GetRenderPath()->OnSceneUpdated();
                }
            }
            else {
                ImGui::Text("No material associated with this entity.");
            }
        }
    }

    void EditorLayer::RefreshModelList()
    {
        m_AvailableModels.clear();
        std::string rootPath = Config::ASSET_DIR + "models";
        
        if (!std::filesystem::exists(rootPath)) {
            CH_CORE_WARN("Model directory not found: {}", rootPath);
            return;
        }

        try {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(rootPath)) {
                if (entry.is_regular_file()) {
                    auto ext = entry.path().extension().string();
                    if (ext == ".gltf" || ext == ".glb" || ext == ".obj") {
                        std::string relPath = std::filesystem::relative(entry.path(), ".").string();
                        std::replace(relPath.begin(), relPath.end(), '\\', '/');
                        
                        m_AvailableModels.push_back({ 
                            entry.path().filename().string(), 
                            relPath 
                        });
                    }
                }
            }
        } catch (const std::exception& e) {
            CH_CORE_ERROR("Error scanning models: {}", e.what());
        }
    }

    void EditorLayer::LoadModel(const std::string& path)
    {
        m_ActiveModelPath = path;
        m_App->LoadScene(path);
    }

    void EditorLayer::DrawMenuBar()
    {
        if (ImGui::BeginMainMenuBar()) 
        {
            if (ImGui::BeginMenu("File")) 
            {
                if (ImGui::MenuItem("Refresh Assets")) RefreshModelList();
                ImGui::Separator();
                if (ImGui::MenuItem("Exit")) m_App->Close();
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    }

    void EditorLayer::DrawStatsPanel()
    {
        if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Frame Time: %.3f ms", m_AverageFrameTime);
            ImGui::Text("FPS: %.1f", m_AverageFPS);
            
            if (m_App->GetRenderPath()) {
                m_App->GetRenderPath()->GetRenderGraph().DrawPerformanceStatistics();
            }
        }
        
        if (ImGui::CollapsingHeader("Camera Controls", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::Button("Reset Camera")) m_EditorCamera.Reset();
            
            float distance = m_EditorCamera.GetDistance();
            if (ImGui::DragFloat("Distance", &distance, 0.1f, 0.1f, 1000.0f))
                m_EditorCamera.SetDistance(distance);
                
            glm::vec3 focalPoint = m_EditorCamera.GetFocalPoint();
            if (ImGui::DragFloat3("Focal Point", &focalPoint.x, 0.1f))
                m_EditorCamera.SetFocalPoint(focalPoint);
        }

        if (ImGui::CollapsingHeader("Scene Stats", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto scene = m_App->GetScene();
            ImGui::Text("Entities: %llu", scene->GetEntities().size());
            ImGui::Text("Current Model: %s", m_ActiveModelPath.empty() ? "None" : m_ActiveModelPath.c_str());
        }
    }

    void EditorLayer::DrawLightSettings()
    {
        auto scene = m_App->GetScene();
        if (!scene) return;

        auto& light = scene->GetLight();
        bool changed = false;

        if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen))
        {
            glm::vec3 dir = glm::vec3(light.direction);
            if (ImGui::DragFloat3("Direction", &dir.x, 0.01f, -1.0f, 1.0f)) {
                light.direction = glm::vec4(glm::normalize(dir), 0.0f);
                changed = true;
            }

            glm::vec3 color = glm::vec3(light.color);
            if (ImGui::ColorEdit3("Color", &color.x)) {
                light.color = glm::vec4(color, 1.0f);
                changed = true;
            }

            float intensity = light.intensity.x;
            if (ImGui::DragFloat("Intensity", &intensity, 0.1f, 0.0f, 100.0f)) {
                light.intensity = glm::vec4(intensity);
                changed = true;
            }
        }

        if (changed && m_App->GetCurrentRenderPathType() == RenderPathType::RayTracing)
            m_App->GetRenderPath()->OnSceneUpdated();
    }

    void EditorLayer::DrawRenderPathPanel()
    {
        const char* modes[] = { "Forward", "Ray Tracing", "Hybrid" };
        int currentIdx = (int)m_App->GetCurrentRenderPathType();
        
        ImGui::Text("Render Path Configuration");
        if (ImGui::Combo("Active Path", &currentIdx, modes, IM_ARRAYSIZE(modes))) 
        {
            m_App->SwitchRenderPath((RenderPathType)currentIdx);
        }

        ImGui::Separator();
        
        DrawLightSettings();

        ImGui::Separator();
        if (m_App->GetRenderPath()) 
        {
            m_App->GetRenderPath()->OnImGui();
        }
    }

    void EditorLayer::DrawModelSelectionPanel()
    {
        if (ImGui::Button("Refresh List")) RefreshModelList();
        
        ImGui::BeginChild("ModelList", ImVec2(0, 300), true);
        for (int i = 0; i < (int)m_AvailableModels.size(); i++) {
            ImGui::PushID(i);
            bool isSelected = (m_SelectedModelIndex == i);
            if (ImGui::Selectable(m_AvailableModels[i].Name.c_str(), isSelected)) {
                m_SelectedModelIndex = i;
                LoadModel(m_AvailableModels[i].Path);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", m_AvailableModels[i].Path.c_str());
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    void EditorLayer::DrawViewport()
    {
        VkCommandBuffer cmd = m_App->GetRenderer()->GetActiveCommandBuffer();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
        ImGui::Begin("Viewport");
        
        m_ViewportHovered = ImGui::IsWindowHovered();
        m_ViewportFocused = ImGui::IsWindowFocused();

        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        if (std::abs(viewportPanelSize.x - m_ViewportSize.x) > 1.0f || std::abs(viewportPanelSize.y - m_ViewportSize.y) > 1.0f) {
            if (viewportPanelSize.x > 100 && viewportPanelSize.y > 100) { // Threshold to avoid tiny/invalid sizes
                m_ViewportSize = glm::vec2(viewportPanelSize.x, viewportPanelSize.y);
                m_App->GetRenderPath()->SetViewportSize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
                m_EditorCamera.SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
            }
        }

        auto* renderPath = m_App->GetRenderPath();
        if (renderPath && renderPath->GetRenderGraph().ContainsImage(m_DebugViewTexture)) {
            auto& img = renderPath->GetRenderGraph().GetImage(m_DebugViewTexture);
            auto& access = renderPath->GetRenderGraph().GetImageAccess(m_DebugViewTexture);

            if (img.handle != VK_NULL_HANDLE) {
                if (cmd != VK_NULL_HANDLE && access.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                    VulkanUtils::InsertImageBarrier(cmd, img.handle,
                        VulkanUtils::IsDepthFormat(img.format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
                        access.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        access.stage_flags, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        access.access_flags, VK_ACCESS_SHADER_READ_BIT);
                    
                    access.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    access.stage_flags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                    access.access_flags = VK_ACCESS_SHADER_READ_BIT;
                }
                
                ImTextureID textureID = m_App->GetImGuiLayer()->GetTextureID(img.view, m_App->GetResourceManager()->GetDefaultSampler());
                if (textureID)
                    ImGui::Image(textureID, viewportPanelSize);
            } else {
                ImGui::Text("Image handle is NULL");
            }
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

}
