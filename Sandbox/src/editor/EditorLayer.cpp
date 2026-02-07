#include "pch.h"
#include "EditorLayer.h"
#include "Core/ImGuiLayer.h"
#include "Core/EngineConfig.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/Backend/Renderer.h"
#include "Utils/VulkanBarrier.h"
#include "Renderer/Backend/VulkanContext.h"
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
            m_AverageFrameTime = ts.GetMilliseconds();        m_AverageFPS = 1.0f / ts.GetSeconds();

        m_EditorCamera.OnUpdate(ts, m_ViewportHovered, m_ViewportFocused);

        // Sync state to Application
        FrameContext context;
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
        DrawMenuBar();
        
        // --- Left Panel: Scene & Assets ---
        ImGui::Begin("Scene & Assets");
        if (ImGui::BeginTabBar("LeftTabs"))
        {
            if (ImGui::BeginTabItem("Hierarchy"))
            {
                DrawSceneHierarchy();
                ImGui::Separator();
                DrawPropertiesPanel();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Assets"))
            {
                DrawModelSelectionPanel();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();

        // --- Right Panel: Settings & Stats ---
        ImGui::Begin("Settings");
        if (ImGui::BeginTabBar("SettingsTabs"))
        {
            if (ImGui::BeginTabItem("Renderer"))
            {
                DrawRenderPathPanel();

                if (ImGui::CollapsingHeader("Lighting & Env", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    if (ImGui::DragFloat3("Light Position", Config::Settings.LightPosition, 0.1f)) {
                        if (m_App->GetCurrentRenderPathType() != RenderPathType::Forward)
                            m_App->GetRenderPath()->OnSceneUpdated();
                    }
                    if (ImGui::DragFloat("Light Intensity", &Config::Settings.LightIntensity, 0.1f, 0.0f, 100.0f)) {
                        if (m_App->GetCurrentRenderPathType() != RenderPathType::Forward)
                            m_App->GetRenderPath()->OnSceneUpdated();
                    }
                    ImGui::ColorEdit3("Light Color", Config::Settings.LightColor);
                    
                    ImGui::Separator();
                    if (ImGui::Button("Load Skybox (HDR)")) {
                        m_App->LoadSkybox(Config::ASSET_DIR + "textures/newport_loft.hdr");
                    }
                }

                if (ImGui::CollapsingHeader("Visuals", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    const char* displayModes[] = { "Final Color", "Shadows", "AO", "Reflections" };
                    ImGui::Combo("Display Mode", &Config::Settings.DisplayMode, displayModes, IM_ARRAYSIZE(displayModes));

                    ImGui::Text("Debug View Selection");
                    if (ImGui::BeginCombo("Viewport Texture", m_DebugViewTexture.c_str())) {
                        std::vector<std::string> views = { RS::FINAL_COLOR };
                        auto* renderPath = m_App->GetRenderPath();
                        if (renderPath) {
                            RenderPathType type = m_App->GetCurrentRenderPathType();
                            std::vector<std::string> colorAtts = renderPath->GetRenderGraph().GetColorAttachments();
                            
                            for (const auto& att : colorAtts) {
                                if (att == RS::FINAL_COLOR) continue;
                                
                                // Logic: Only show relevant buffers for the mode
                                if (type == RenderPathType::Forward) {
                                    // Forward doesn't have G-Buffer or RT buffers
                                    if (att == RS::FORWARD_COLOR) views.push_back(att);
                                } else {
                                    views.push_back(att);
                                }
                            }
                            if (renderPath->GetRenderGraph().ContainsImage(RS::DEPTH))
                                views.push_back(RS::DEPTH);
                        }

                        for (const auto& v : views) {
                            if (ImGui::Selectable(v.c_str(), m_DebugViewTexture == v)) 
                                m_DebugViewTexture = v;
                        }
                        ImGui::EndCombo();
                    }
                }

                if (ImGui::Button("Compile & Reload Shaders")) {
                    m_App->RecompileShaders();
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Stats"))
            {
                DrawStatsPanel();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Camera"))
            {
                if (ImGui::Button("Reset Camera")) m_EditorCamera.Reset();

                glm::vec3 pos = m_EditorCamera.GetPosition();
                float pitch = m_EditorCamera.GetPitch();
                float yaw = m_EditorCamera.GetYaw();
                float fov = m_EditorCamera.GetFOV();
                float dist = m_EditorCamera.GetDistance();

                ImGui::Text("Position: %.2f, %.2f, %.2f", pos.x, pos.y, pos.z);
                ImGui::Text("Pitch: %.2f, Yaw: %.2f", glm::degrees(pitch), glm::degrees(yaw));
                ImGui::Text("Distance: %.2f", dist);
                
                if (ImGui::SliderFloat("FOV", &fov, 10.0f, 120.0f))
                    m_EditorCamera.SetFOV(fov);
                
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
        
        ImGui::End();

        DrawViewport();
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
        if (ImGui::CollapsingHeader("Frame Timing", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("CPU Frame Time: %.3f ms", m_AverageFrameTime);
            ImGui::Text("CPU FPS: %.1f", m_AverageFPS);
        }
        
        if (ImGui::CollapsingHeader("GPU Profiler", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (m_App->GetRenderPath()) {
                m_App->GetRenderPath()->GetRenderGraph().DrawPerformanceStatistics();
            } else {
                ImGui::Text("No active Render Path.");
            }
        }

        if (ImGui::CollapsingHeader("Scene Info", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto scene = m_App->GetScene();
            ImGui::Text("Entities: %llu", scene->GetEntities().size());
            ImGui::Text("Materials: %llu", m_App->GetResourceManager()->GetMaterials().size());
            ImGui::Text("Textures: %llu", m_App->GetResourceManager()->GetTextures().size());
            ImGui::Text("Current Model:");
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "  %s", m_ActiveModelPath.empty() ? "None" : m_ActiveModelPath.c_str());
        }
    }

    void EditorLayer::DrawRenderPathPanel()
    {
        const char* modes[] = { "Forward", "Ray Tracing", "Hybrid" };
        int currentIdx = (int)m_App->GetCurrentRenderPathType();
        
        if (ImGui::Combo("Render Mode", &currentIdx, modes, IM_ARRAYSIZE(modes))) 
        {
            m_App->SwitchRenderPath((RenderPathType)currentIdx);
        }

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
            if (viewportPanelSize.x > 0 && viewportPanelSize.y > 0) {
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
