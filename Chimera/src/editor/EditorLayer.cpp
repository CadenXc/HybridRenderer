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
        m_FrameTimeHistory.resize(100, 0.0f);
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
        if (m_UpdateTimer > 0.1f)
        {
            m_UpdateTimer = 0.0f;
            for (size_t i = 0; i < m_FrameTimeHistory.size() - 1; i++) {
                m_FrameTimeHistory[i] = m_FrameTimeHistory[i + 1];
            }
            m_FrameTimeHistory[m_FrameTimeHistory.size() - 1] = ts * 1000.0f;
        }
    }

    void EditorLayer::OnUIRender()
    {
        DrawMenuBar();

        if (m_ShowStats) DrawStatsPanel();
        if (m_ShowHierarchy) DrawHierarchyPanel();
        if (m_ShowInspector) DrawInspectorPanel();
        
        DrawResourceBrowserPanel();

        if (m_ShowDemoWindow)
            ImGui::ShowDemoWindow(&m_ShowDemoWindow);
    }

    void EditorLayer::RefreshModelList()
    {
        m_AvailableModels.clear();
        if (std::filesystem::exists(m_CurrentLoadPath)) {
            for (const auto& entry : std::filesystem::directory_iterator(m_CurrentLoadPath)) {
                if (entry.path().extension() == ".obj" || entry.path().extension() == ".gltf" || entry.path().extension() == ".glb") {
                    m_AvailableModels.push_back(entry.path().string());
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
                if (ImGui::MenuItem("Exit")) m_App->Close();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Window"))
            {
                ImGui::MenuItem("Stats", nullptr, &m_ShowStats);
                ImGui::MenuItem("Hierarchy", nullptr, &m_ShowHierarchy);
                ImGui::MenuItem("Inspector", nullptr, &m_ShowInspector);
                ImGui::Separator();
                ImGui::MenuItem("ImGui Demo", nullptr, &m_ShowDemoWindow);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Render Path"))
            {
                if (ImGui::MenuItem("Forward")) m_App->SwitchRenderPath(RenderPathType::Forward);
                if (ImGui::MenuItem("Hybrid (Deferred)")) m_App->SwitchRenderPath(RenderPathType::Hybrid);
                if (ImGui::MenuItem("Ray Tracing")) m_App->SwitchRenderPath(RenderPathType::RayTracing);
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }
    }

    void EditorLayer::DrawStatsPanel()
    {
        ImGui::Begin("Statistics");
        
        float avg = 0.0f;
        for (float f : m_FrameTimeHistory) avg += f;
        avg /= m_FrameTimeHistory.size();

        ImGui::Text("Average Frame Time: %.3f ms (%.1f FPS)", avg, 1000.0f / avg);
        
        ImGui::PlotLines("Frame Time", m_FrameTimeHistory.data(), (int)m_FrameTimeHistory.size(), 0, nullptr, 0.0f, 33.0f, ImVec2(0, 80));

        ImGui::Separator();
        ImGui::Text("Renderer: Vulkan 1.3");
        
        ImGui::End();
    }

    void EditorLayer::DrawHierarchyPanel()
    {
        ImGui::Begin("Hierarchy");
        
        if (ImGui::TreeNodeEx("Scene Root", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::Selectable("Main Camera")) {}
            if (ImGui::Selectable("Directional Light")) {}
            if (ImGui::Selectable("Main Model")) {}
            
            ImGui::TreePop();
        }
        
        ImGui::End();
    }

    void EditorLayer::DrawInspectorPanel()
    {
        ImGui::Begin("Inspector");

        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // Simple placeholder for transform editing
            static glm::vec3 pos(0.0f), rot(0.0f), scale(1.0f);
            DrawVec3Control("Position", pos);
            DrawVec3Control("Rotation", rot);
            DrawVec3Control("Scale", scale, 1.0f);
        }

        if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
        {
            static float roughness = 0.5f;
            static float metallic = 0.0f;
            static glm::vec3 albedo(1.0f);
            
            ImGui::ColorEdit3("Albedo", glm::value_ptr(albedo));
            ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f);
            ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f);
        }

        ImGui::End();
    }

    void EditorLayer::DrawResourceBrowserPanel()
    {
        ImGui::Begin("Resource Browser");

        if (ImGui::Button("Refresh")) RefreshModelList();
        
        ImGui::Text("Models in %s:", m_CurrentLoadPath.c_str());
        ImGui::BeginChild("FileList");
        for (int i = 0; i < (int)m_AvailableModels.size(); i++)
        {
            if (ImGui::Selectable(m_AvailableModels[i].c_str(), m_SelectedModelIndex == i))
            {
                m_SelectedModelIndex = i;
            }
            
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
            {
                m_App->LoadScene(m_AvailableModels[i]);
            }
        }
        ImGui::EndChild();

        ImGui::End();
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

        float lineHeight = ImGui::GetFontSize() + GImGui->Style.FramePadding.y * 2.0f;
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
}