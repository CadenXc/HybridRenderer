#include "pch.h"
#include "EditorLayer.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <filesystem>
#include <glm/gtc/type_ptr.hpp>

namespace Chimera {

    EditorLayer::EditorLayer(Application* app)
        : m_App(app)
    {
        m_FrameTimeHistory.resize(120, 0.0f);
        m_FpsHistory.resize(120, 0.0f);
    }

    void EditorLayer::OnAttach()
    {
        RefreshModelList();
    }

    void EditorLayer::OnDetach()
    {
    }

    void EditorLayer::OnUpdate(float ts)
    {
        m_UpdateTimer += ts;
        if (m_UpdateTimer > 0.016f) // Update at roughly 60Hz
        {
            m_UpdateTimer = 0.0f;
            
            // Shift frame time history
            for (size_t i = 0; i < m_FrameTimeHistory.size() - 1; i++) {
                m_FrameTimeHistory[i] = m_FrameTimeHistory[i + 1];
                m_FpsHistory[i] = m_FpsHistory[i + 1];
            }
            
            float frameTimeMs = ts * 1000.0f;
            m_FrameTimeHistory[m_FrameTimeHistory.size() - 1] = frameTimeMs;
            m_FpsHistory[m_FpsHistory.size() - 1] = 1000.0f / frameTimeMs;

            // Calculate average
            m_AverageFrameTime = 0.0f;
            for (float f : m_FrameTimeHistory) {
                m_AverageFrameTime += f;
            }
            m_AverageFrameTime /= m_FpsHistory.size();
        }
    }

    void EditorLayer::OnUIRender()
    {
        DrawMenuBar();

        // Windows
        if (m_ShowStats) DrawStatsPanel();
        if (m_ShowScene) DrawScenePanel();
        if (m_ShowInspector) DrawInspectorPanel();
        if (m_ShowResourceBrowser) DrawResourceBrowserPanel();
        if (m_ShowDebug) DrawDebugPanel();
        if (m_ShowRenderPathSettings) DrawRenderPathPanel();
        
        if (m_ShowDemoWindow)
            ImGui::ShowDemoWindow(&m_ShowDemoWindow);
    }

    void EditorLayer::RefreshModelList()
    {
        m_AvailableModels.clear();
        if (std::filesystem::exists(m_CurrentLoadPath)) {
            for (const auto& entry : std::filesystem::directory_iterator(m_CurrentLoadPath)) {
                if (entry.path().extension() == ".obj" || 
                    entry.path().extension() == ".gltf" || 
                    entry.path().extension() == ".glb") {
                    m_AvailableModels.push_back(entry.path().filename().string());
                }
            }
        }
    }

    void EditorLayer::DrawMenuBar()
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Exit", "Alt+F4")) m_App->Close();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
                ImGui::MenuItem("Statistics", nullptr, &m_ShowStats);
                ImGui::MenuItem("Scene", nullptr, &m_ShowScene);
                ImGui::MenuItem("Inspector", nullptr, &m_ShowInspector);
                ImGui::MenuItem("Resources", nullptr, &m_ShowResourceBrowser);
                ImGui::MenuItem("Render Path", nullptr, &m_ShowRenderPathSettings);
                ImGui::Separator();
                ImGui::MenuItem("Debug", nullptr, &m_ShowDebug);
                ImGui::MenuItem("ImGui Demo", nullptr, &m_ShowDemoWindow);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Render Path"))
            {
                if (ImGui::MenuItem("Forward", nullptr, m_App->GetCurrentRenderPathType() == RenderPathType::Forward))
                    m_App->SwitchRenderPath(RenderPathType::Forward);
                if (ImGui::MenuItem("Ray Tracing", nullptr, m_App->GetCurrentRenderPathType() == RenderPathType::RayTracing))
                    m_App->SwitchRenderPath(RenderPathType::RayTracing);
                ImGui::EndMenu();
            }

            ImGui::Spacing();
            ImGui::Spacing();
            
            // Status bar on right side
            float fps = (m_AverageFrameTime > 0.0f) ? (1000.0f / m_AverageFrameTime) : 0.0f;
            ImGui::Text("FPS: %.1f | Frame: %.2f ms", fps, m_AverageFrameTime);

            ImGui::EndMainMenuBar();
        }
    }

    void EditorLayer::DrawStatsPanel()
    {
        ImGui::SetNextWindowPos(ImVec2(10, 30), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(350, 250), ImGuiCond_FirstUseEver);
        
        if (ImGui::Begin("Statistics", &m_ShowStats, ImGuiWindowFlags_NoMove))
        {
            ImGui::Text("Performance Metrics");
            DrawSeparator();

            float fps = (m_AverageFrameTime > 0.0f) ? (1000.0f / m_AverageFrameTime) : 0.0f;
            ImGui::Text("Frame Time: %.3f ms", m_AverageFrameTime);
            ImGui::SameLine();
            ImGui::TextColored(fps >= 60 ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : 
                                         ImVec4(1.0f, 1.0f, 0.0f, 1.0f), 
                               "FPS: %.1f", fps);
            
            ImGui::PlotLines("Frame Time (ms)", m_FrameTimeHistory.data(), 
                           (int)m_FrameTimeHistory.size(), 0, nullptr, 0.0f, 33.0f, ImVec2(0, 80));

            DrawSeparator();
            ImGui::Text("System Information");
            ImGui::BulletText("API: Vulkan 1.3");
            ImGui::BulletText("Device: GPU");
            
            ImGui::End();
        }
    }

    void EditorLayer::DrawScenePanel()
    {
        ImGui::SetNextWindowPos(ImVec2(10, 300), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);
        
        if (ImGui::Begin("Scene", &m_ShowScene))
        {
            ImGui::Text("Scene Hierarchy");
            DrawSeparator();
            
            if (ImGui::TreeNodeEx("Scene Root", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Selectable("Main Camera");
                ImGui::Selectable("Directional Light");
                ImGui::Selectable("Main Model");
                ImGui::TreePop();
            }
            
            ImGui::End();
        }
    }

    void EditorLayer::DrawInspectorPanel()
    {
        ImGui::SetNextWindowPos(ImVec2(1200, 30), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(380, 600), ImGuiCond_FirstUseEver);
        
        if (ImGui::Begin("Inspector", &m_ShowInspector))
        {
            ImGui::Text("Properties");
            DrawSeparator();

            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
            {
                static glm::vec3 pos(0.0f), rot(0.0f), scale(1.0f);
                ImGui::DragFloat3("Position##pos", glm::value_ptr(pos), 0.1f);
                ImGui::DragFloat3("Rotation##rot", glm::value_ptr(rot), 0.1f);
                ImGui::DragFloat3("Scale##scale", glm::value_ptr(scale), 0.1f);
            }

            if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
            {
                static float roughness = 0.5f;
                static float metallic = 0.0f;
                static glm::vec3 albedo(1.0f);
                
                ImGui::ColorEdit3("Albedo", glm::value_ptr(albedo));
                ImGui::SliderFloat("Roughness##mat", &roughness, 0.0f, 1.0f);
                ImGui::SliderFloat("Metallic##mat", &metallic, 0.0f, 1.0f);
            }

            if (ImGui::CollapsingHeader("Advanced", ImGuiTreeNodeFlags_DefaultOpen))
            {
                static bool castShadow = true;
                static bool receiveShadow = true;
                ImGui::Checkbox("Cast Shadow", &castShadow);
                ImGui::Checkbox("Receive Shadow", &receiveShadow);
            }

            ImGui::End();
        }
    }

    void EditorLayer::DrawResourceBrowserPanel()
    {
        ImGui::SetNextWindowPos(ImVec2(1200, 650), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(380, 300), ImGuiCond_FirstUseEver);
        
        if (ImGui::Begin("Resources", &m_ShowResourceBrowser))
        {
            ImGui::Text("Model Browser");
            DrawSeparator();

            if (ImGui::Button("Refresh Models")) RefreshModelList();
            
            ImGui::Text("Available Models: %zu", m_AvailableModels.size());
            ImGui::BeginChild("ModelList", ImVec2(0, -30), true);
            
            for (int i = 0; i < (int)m_AvailableModels.size(); i++)
            {
                bool isSelected = (m_SelectedModelIndex == i);
                if (ImGui::Selectable(m_AvailableModels[i].c_str(), isSelected))
                {
                    m_SelectedModelIndex = i;
                }
                
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
                
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                {
                    m_App->LoadScene(m_CurrentLoadPath + "/" + m_AvailableModels[i]);
                }
            }
            ImGui::EndChild();

            ImGui::End();
        }
    }

    void EditorLayer::DrawDebugPanel()
    {
        ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
        
        if (ImGui::Begin("Debug", &m_ShowDebug))
        {
            ImGui::Text("Debug Information");
            DrawSeparator();
            
            if (ImGui::TreeNodeEx("Performance", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::PlotLines("FPS History", m_FpsHistory.data(), 
                               (int)m_FpsHistory.size(), 0, nullptr, 0.0f, 120.0f, ImVec2(0, 80));
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx("Memory", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Text("GPU Memory: N/A");
                ImGui::Text("CPU Memory: N/A");
                ImGui::TreePop();
            }

            ImGui::End();
        }
    }

    void EditorLayer::DrawRenderPathPanel()
    {
        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
        
        if (ImGui::Begin("Render Path Settings", &m_ShowRenderPathSettings))
        {
            ImGui::Text("Render Configuration");
            DrawSeparator();
            
            if (ImGui::TreeNodeEx("Forward Renderer", ImGuiTreeNodeFlags_DefaultOpen))
            {
                static bool vsync = true;
                static int msaa = 1;
                ImGui::Checkbox("VSync", &vsync);
                ImGui::Combo("MSAA", &msaa, "Off\0x2\0x4\0x8\0x16\0");
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx("Ray Tracing", ImGuiTreeNodeFlags_DefaultOpen))
            {
                static int maxDepth = 2;
                static int samplesPerPixel = 1;
                ImGui::SliderInt("Max Depth", &maxDepth, 1, 10);
                ImGui::SliderInt("Samples Per Pixel", &samplesPerPixel, 1, 64);
                ImGui::TreePop();
            }

            ImGui::End();
        }
    }

    void EditorLayer::DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue, float columnWidth)
    {
        ImGui::PushID(label.c_str());

        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, columnWidth);
        ImGui::Text(label.c_str());
        ImGui::NextColumn();

        ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 0 });

        float lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
        ImVec2 buttonSize = { lineHeight + 3.0f, lineHeight };

        if (ImGui::Button("X", buttonSize))
            values.x = resetValue;
        ImGui::SameLine();
        ImGui::DragFloat("##X", &values.x, 0.1f);
        ImGui::PopItemWidth();
        ImGui::SameLine();

        if (ImGui::Button("Y", buttonSize))
            values.y = resetValue;
        ImGui::SameLine();
        ImGui::DragFloat("##Y", &values.y, 0.1f);
        ImGui::PopItemWidth();
        ImGui::SameLine();

        if (ImGui::Button("Z", buttonSize))
            values.z = resetValue;
        ImGui::SameLine();
        ImGui::DragFloat("##Z", &values.z, 0.1f);
        ImGui::PopItemWidth();

        ImGui::PopStyleVar();
        ImGui::Columns(1);
        ImGui::PopID();
    }

    void EditorLayer::DrawSeparator()
    {
        ImGui::Separator();
    }
}
