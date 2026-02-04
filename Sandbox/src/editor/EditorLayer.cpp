#include "pch.h"
#include "EditorLayer.h"
#include "Core/EngineConfig.h"
#include "Core/ImGuiLayer.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/Backend/Renderer.h"
#include "Utils/VulkanBarrier.h"
#include "Renderer/Backend/VulkanContext.h"
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
        m_AverageFrameTime = ts.GetMilliseconds();
        m_AverageFPS = 1.0f / ts.GetSeconds();

        m_EditorCamera.OnUpdate(ts);

        // Sync state to Application
        FrameContext context;
        context.View = m_EditorCamera.GetViewMatrix();
        context.Projection = m_EditorCamera.GetProjection();
        context.CameraPosition = m_EditorCamera.GetPosition();
        context.ViewportSize = m_ViewportSize;
        context.DeltaTime = ts.GetSeconds();
        context.Time = (float)glfwGetTime();
        context.FrameIndex = m_App->GetRenderer()->GetCurrentFrameIndex();
        m_App->SetFrameContext(context);
    }

    void EditorLayer::OnEvent(Event& e)
    {
        m_EditorCamera.OnEvent(e);
    }

    void EditorLayer::OnUIRender()
    {
        DrawMenuBar();
        
        // Panels
        ImGui::Begin("Editor", nullptr);
        
        if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen))
            DrawStatsPanel();

        if (ImGui::CollapsingHeader("Render Settings", ImGuiTreeNodeFlags_DefaultOpen))
            DrawRenderPathPanel();

        if (ImGui::CollapsingHeader("Asset Browser", ImGuiTreeNodeFlags_DefaultOpen))
            DrawModelSelectionPanel();

        ImGui::End();

        DrawSettingsPanel();
        DrawViewport();
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
                        // 替换反斜杠为正斜杠，保持路径一致�?
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
        ImGui::Text("CPU Frame Time: %.3f ms", m_AverageFrameTime);
        ImGui::Text("CPU FPS: %.1f", m_AverageFPS);
        
        ImGui::Separator();
        ImGui::Text("GPU Performance (ms):");
        if (m_App->GetRenderPath()) {
            m_App->GetRenderPath()->GetRenderGraph().GatherPerformanceStatistics();
            m_App->GetRenderPath()->GetRenderGraph().DrawPerformanceStatistics();
        }

        ImGui::Separator();
        ImGui::Text("Current Model:");
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", m_ActiveModelPath.empty() ? "None" : m_ActiveModelPath.c_str());
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
        // 允许当前渲染路径暴露自己的调�?UI
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

    void EditorLayer::DrawSettingsPanel()
    {
        ImGui::Begin("Settings");
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        if (ImGui::Button("Compile & Reload Shaders")) {
            m_App->RecompileShaders();
        }

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
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
        }
        
        ImGui::Separator();
        ImGui::Text("Debug View Selection");
        if (ImGui::BeginCombo("Viewport Texture", m_DebugViewTexture.c_str())) {
            std::vector<std::string> views = { RS::FINAL_COLOR };
            auto* renderPath = m_App->GetRenderPath();
            if (renderPath) {
                std::vector<std::string> colorAtts = renderPath->GetRenderGraph().GetColorAttachments();
                for (const auto& att : colorAtts) {
                    if (att != RS::FINAL_COLOR) views.push_back(att);
                }
                // Add depth if it exists
                if (renderPath->GetRenderGraph().ContainsImage(RS::DEPTH))
                    views.push_back(RS::DEPTH);
            }

            for (const auto& v : views) {
                if (ImGui::Selectable(v.c_str(), m_DebugViewTexture == v)) 
                    m_DebugViewTexture = v;
            }
            ImGui::EndCombo();
        }
        ImGui::End();
    }

    void EditorLayer::DrawViewport()
    {
        VkCommandBuffer cmd = m_App->GetRenderer()->GetActiveCommandBuffer();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
        ImGui::Begin("Viewport");
        
        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        if (std::abs(viewportPanelSize.x - m_ViewportSize.x) > 1.0f || std::abs(viewportPanelSize.y - m_ViewportSize.y) > 1.0f) {
            if (viewportPanelSize.x > 0 && viewportPanelSize.y > 0) {
                m_ViewportSize = glm::vec2(viewportPanelSize.x, viewportPanelSize.y);
                m_App->GetRenderPath()->OnSceneUpdated();
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

    void EditorLayer::DrawSceneHierarchy()
    {
    }

}
