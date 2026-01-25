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
        // 预分配图表数据空间
        m_FrameTimeHistory.resize(50, 0.0f);
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
        // 更新帧时间历史数据 (限制更新频率，避免闪烁)
        m_UpdateTimer += ts;
        if (m_UpdateTimer > 0.1f)
        {
            m_UpdateTimer = 0.0f;
            // 简单的数据移位，模拟队列
            for (size_t i = 0; i < m_FrameTimeHistory.size() - 1; i++) {
                m_FrameTimeHistory[i] = m_FrameTimeHistory[i + 1];
            }
            m_FrameTimeHistory[m_FrameTimeHistory.size() - 1] = ts * 1000.0f; // 存毫秒
        }
    }

    void EditorLayer::OnUIRender()
    {
        // 1. 绘制菜单栏
        DrawMenuBar();

        // 2. 绘制各个子面板
        if (m_ShowStats) DrawStatsPanel();
        if (m_ShowHierarchy) DrawHierarchyPanel();
        if (m_ShowInspector) DrawInspectorPanel();
        
        // 资源浏览器 (整合在一起)
        DrawResourceBrowserPanel();

        if (m_ShowDemoWindow)
            ImGui::ShowDemoWindow(&m_ShowDemoWindow);
    }

    void EditorLayer::DrawMenuBar()
    {
        if (ImGui::BeginMainMenuBar()) 
        {
            if (ImGui::BeginMenu("File")) 
            {
                if (ImGui::MenuItem("Exit", "Alt+F4")) {
                    glfwSetWindowShouldClose(m_App->GetWindow(), true);
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
                ImGui::MenuItem("Stats", nullptr, &m_ShowStats);
                ImGui::MenuItem("Hierarchy", nullptr, &m_ShowHierarchy);
                ImGui::MenuItem("Inspector", nullptr, &m_ShowInspector);
                ImGui::MenuItem("ImGui Demo", nullptr, &m_ShowDemoWindow);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Renderer")) 
            {
                // 获取当前渲染模式并绘制单选框
                RenderPathType currentType = m_App->GetCurrentRenderPathType();
                if (ImGui::MenuItem("Forward Rasterization", nullptr, currentType == RenderPathType::Forward))
                    m_App->SwitchRenderPath(RenderPathType::Forward);
                if (ImGui::MenuItem("Ray Tracing", nullptr, currentType == RenderPathType::RayTracing))
                    m_App->SwitchRenderPath(RenderPathType::RayTracing);
                if (ImGui::MenuItem("Hybrid", nullptr, currentType == RenderPathType::Hybrid))
                    m_App->SwitchRenderPath(RenderPathType::Hybrid);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    }

    void EditorLayer::DrawStatsPanel()
    {
        ImGui::Begin("Statistics", &m_ShowStats);
        
        float fps = ImGui::GetIO().Framerate;
        float ms = 1000.0f / fps;

        ImGui::Text("FPS: %.1f", fps);
        ImGui::Text("Frame Time: %.3f ms", ms);

        // 绘制波形图
        ImGui::PlotLines("##FrameTimes", m_FrameTimeHistory.data(), (int)m_FrameTimeHistory.size(), 0, "Frame Time (ms)", 0.0f, 33.0f, ImVec2(0, 80));
        
        ImGui::Separator();
        ImGui::Text("Renderer: Vulkan");
        ImGui::End();
    }

    void EditorLayer::DrawHierarchyPanel()
    {
        ImGui::Begin("Scene Hierarchy", &m_ShowHierarchy);

        // 模拟场景树
        if (ImGui::TreeNodeEx("Scene Root", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed))
        {
            // 模拟选中 Viking Room
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Selected;
            if (ImGui::TreeNodeEx("Viking Room", flags)) {
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx("Directional Light", ImGuiTreeNodeFlags_Leaf)) {
                ImGui::TreePop();
            }

            ImGui::TreePop();
        }
        ImGui::End();
    }

    void EditorLayer::DrawInspectorPanel()
    {
        ImGui::Begin("Inspector", &m_ShowInspector);

        // 这里模拟属性编辑，实际上应该绑定 m_App->GetScene()->GetSelectedEntity()
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            static glm::vec3 translation(0.0f);
            static glm::vec3 rotation(0.0f);
            static glm::vec3 scale(1.0f);

            DrawVec3Control("Translation", translation);
            DrawVec3Control("Rotation", rotation);
            DrawVec3Control("Scale", scale, 1.0f);
        }

        if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
        {
            static glm::vec4 color(1.0f);
            static float roughness = 0.5f;
            static float metallic = 0.0f;
            
            ImGui::ColorEdit4("Base Color", glm::value_ptr(color));
            ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f);
            ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f);
        }

        ImGui::End();
    }

    void EditorLayer::DrawResourceBrowserPanel()
    {
        ImGui::Begin("Content Browser");

        if (ImGui::Button("Refresh")) RefreshModelList();
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", m_CurrentLoadPath.c_str());
        ImGui::Separator();

        // 网格布局
        static float padding = 16.0f;
        static float thumbnailSize = 64.0f;
        float cellSize = thumbnailSize + padding;

        float panelWidth = ImGui::GetContentRegionAvail().x;
        int columnCount = (int)(panelWidth / cellSize);
        if (columnCount < 1) columnCount = 1;

        if (ImGui::BeginTable("BrowserTable", columnCount))
        {
            for (const auto& modelName : m_AvailableModels)
            {
                ImGui::TableNextColumn();
                ImGui::PushID(modelName.c_str());

                // 图标按钮 (目前用文字代替，以后可以用 icon texture)
                if (ImGui::Button(modelName.c_str(), ImVec2(thumbnailSize, thumbnailSize)))
                {
                    std::string fullPath = m_CurrentLoadPath + "/" + modelName;
                    CH_INFO("Loading model: {}", fullPath);
                    m_App->LoadScene(fullPath);
                }
                
                // 文件名下方显示
                ImGui::TextWrapped("%s", modelName.c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        ImGui::End();
    }

    void EditorLayer::RefreshModelList()
    {
        m_AvailableModels.clear();
        m_SelectedModelIndex = -1;

        // 确保使用 std::filesystem
        namespace fs = std::filesystem;
        fs::path path(m_CurrentLoadPath);

        if (fs::exists(path) && fs::is_directory(path)) 
        {
            for (const auto& entry : fs::directory_iterator(path)) 
            {
                if (entry.is_regular_file()) 
                {
                    std::string ext = entry.path().extension().string();
                    if (ext == ".obj" || ext == ".glb" || ext == ".gltf") 
                    {
                        m_AvailableModels.push_back(entry.path().filename().string());
                    }
                }
            }
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

        // X Axis
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.8f, 0.1f, 0.15f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.9f, 0.2f, 0.2f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.8f, 0.1f, 0.15f, 1.0f });
        if (ImGui::Button("X", buttonSize)) values.x = resetValue;
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::SameLine();

        // Y Axis
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.3f, 0.8f, 0.3f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });
        if (ImGui::Button("Y", buttonSize)) values.y = resetValue;
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::SameLine();

        // Z Axis
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.2f, 0.35f, 0.9f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });
        if (ImGui::Button("Z", buttonSize)) values.z = resetValue;
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::DragFloat("##Z", &values.z, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();

        ImGui::PopStyleVar();
        ImGui::Columns(1);
        ImGui::PopID();
    }
}