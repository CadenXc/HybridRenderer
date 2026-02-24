#include "pch.h"
#include "EditorLayer.h"
#include "Core/Application.h"
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
#include <imgui_internal.h>
#include <filesystem>
#include "Renderer/Pipelines/RenderPathFactory.h"
#include "Renderer/RenderState.h"
#include "Utils/VulkanScreenshot.h"

namespace Chimera
{

    EditorLayer::EditorLayer()
        : Layer("EditorLayer"),
        m_EditorCamera(45.0f, 1.778f, 0.1f, 1000.0f)
    {
        m_ShowControlPanel = true;
        m_ShowViewport = true;

        auto& app = Application::Get();
        m_ViewportSize = { (float)app.GetWindow().GetWidth(), (float)app.GetWindow().GetHeight() };

        m_EditorCamera.SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
        m_EditorCamera.SetFocalPoint({ 0.0f, 1.0f, 0.0f });
        m_EditorCamera.SetDistance(5.0f);

        // [INITIAL] Enable core Hybrid features by default
        m_RenderFlags = RENDER_FLAG_SVGF_BIT | RENDER_FLAG_GI_BIT | RENDER_FLAG_SHADOW_BIT | RENDER_FLAG_REFLECTION_BIT | RENDER_FLAG_TAA_BIT;

        auto scene = std::make_shared<Scene>(app.GetContext());
        ResourceManager::Get().SetActiveScene(scene);

        SwitchRenderPath(RenderPathType::Hybrid);
    }

    void EditorLayer::OnAttach()
    {
        RefreshModelList();
        LoadScene(Config::ASSET_DIR + "models/pica_pica_-_machines/scene.gltf");
    }

    void EditorLayer::OnDetach()
    {
    }

    void EditorLayer::OnUpdate(Timestep ts)
    {
        m_AverageFrameTime = ts.GetMilliseconds();
        m_AverageFPS = 1.0f / ts.GetSeconds();

        if (m_ResizeTimer > 0.0f)
        {
            m_ResizeTimer -= ts.GetSeconds();
            if (m_ResizeTimer <= 0.0f && !m_ResizePending)
            {
                uint32_t w = (uint32_t)m_NextViewportSize.x;
                uint32_t h = (uint32_t)m_NextViewportSize.y;
                m_ResizePending = true;

                Application::Get().QueueEvent([this, w, h]()
                {
                    vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
                    Renderer::Get().OnResize(w, h);
                    if (GetRenderPath())
                    {
                        GetRenderPath()->SetViewportSize(w, h);
                    }
                    m_EditorCamera.SetViewportSize((float)w, (float)h);
                    m_ViewportSize = { (float)w, (float)h };
                    m_ResizePending = false;
                });
            }
        }

        if (m_ResizePending) return;

        m_EditorCamera.OnUpdate(ts, m_ViewportHovered, m_ViewportFocused);

        AppFrameContext context;
        context.View = m_EditorCamera.GetViewMatrix();
        context.Projection = m_EditorCamera.GetProjection();
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
        
        context.SVGFAlphaColor = m_SVGFAlphaColor;
        context.SVGFAlphaMoments = m_SVGFAlphaMoments;
        context.SVGFPhiColor = m_SVGFPhiColor;
        context.SVGFPhiNormal = m_SVGFPhiNormal;
        context.SVGFPhiDepth = m_SVGFPhiDepth;
        context.LightRadius = m_LightRadius;

        RenderPath* activePath = GetRenderPath();
        RenderPathType currentType = activePath ? activePath->GetType() : RenderPathType::Forward;

        if ((m_RenderFlags & RENDER_FLAG_TAA_BIT) && currentType == RenderPathType::Hybrid)
        {
            static const float haltonX[] = { 0.5f, 0.25f, 0.75f, 0.125f, 0.625f, 0.375f, 0.875f, 0.0625f };
            static const float haltonY[] = { 0.333f, 0.666f, 0.111f, 0.444f, 0.777f, 0.222f, 0.555f, 0.888f };
            uint32_t jitterIdx = Application::Get().GetTotalFrameCount() % 8;
            glm::vec2 jitter = { (haltonX[jitterIdx] - 0.5f) / m_ViewportSize.x, (haltonY[jitterIdx] - 0.5f) / m_ViewportSize.y };
            context.Projection = glm::translate(glm::mat4(1.0f), glm::vec3(jitter.x, jitter.y, 0.0f)) * context.Projection;
        }

        Application::Get().SetFrameContext(context);
        Application::Get().SetActiveScene(GetActiveSceneRaw());

        if (activePath && Renderer::HasInstance() && Renderer::Get().IsFrameInProgress())
        {
            RenderFrameInfo frameInfo{};
            frameInfo.commandBuffer = Renderer::Get().GetActiveCommandBuffer();
            frameInfo.frameIndex = Renderer::Get().GetCurrentFrameIndex();
            frameInfo.imageIndex = Renderer::Get().GetCurrentImageIndex();
            frameInfo.globalSet = Application::Get().GetRenderState()->GetDescriptorSet(frameInfo.frameIndex);
            activePath->Render(frameInfo);
        }
    }

    void EditorLayer::OnEvent(Event& e)
    {
        if (m_ViewportHovered) m_EditorCamera.OnEvent(e);
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
            }
        });
    }

    void EditorLayer::LoadScene(const std::string& path)
    {
        Application::Get().QueueEvent([this, path]()
        {
            vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
            GetActiveSceneRaw()->LoadModel(path);
            if (GetRenderPath()) GetRenderPath()->OnSceneUpdated();
        });
    }

    void EditorLayer::ClearScene()
    {
        Application::Get().QueueEvent([this]()
        {
            vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
            ResourceManager::Get().SetActiveScene(std::make_shared<Scene>(Application::Get().GetContext()));
        });
    }

    void EditorLayer::OnImGuiRender()
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
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
            ImGuiID dock_right_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.25f, nullptr, &dock_main_id);
            ImGui::DockBuilderDockWindow("Viewport", dock_main_id);
            ImGui::DockBuilderDockWindow("Control Panel", dock_right_id);
            ImGui::DockBuilderFinish(dockspace_id);
        }
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

        DrawMenuBar();

        RenderPath* activePath = GetRenderPath();

        ImGui::Begin("Viewport", &m_ShowViewport);
        DrawViewportContent(activePath);
        ImGui::End();

        ImGui::Begin("Control Panel", &m_ShowControlPanel);
        DrawControlPanelContent(activePath);
        ImGui::End();

        ImGui::End();
    }

    void EditorLayer::DrawSceneHierarchy()
    {
        Scene* scene = GetActiveSceneRaw();
        if (!scene) return;
        if (ImGui::Button("Clear Scene"))
        {
            ClearScene();
            m_SelectedInstanceIndex = -1;
        }
        ImGui::Separator();
        const auto& entities = scene->GetEntities();
        for (int i = 0; i < (int)entities.size(); i++)
        {
            ImGui::PushID(i);
            if (ImGui::Selectable((entities[i].name + "##" + std::to_string(i)).c_str(), m_SelectedInstanceIndex == i)) m_SelectedInstanceIndex = i;
            ImGui::PopID();
        }
    }

    void EditorLayer::DrawPropertiesPanel(RenderPath* activePath)
    {
        Scene* scene = GetActiveSceneRaw();
        if (!scene || m_SelectedInstanceIndex < 0)
        {
            ImGui::Text("Select an object.");
            return;
        }

        const auto& entities = scene->GetEntities();
        auto& entity = entities[m_SelectedInstanceIndex];
        ImGui::Text("Name: %s", entity.name.c_str());

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

    void EditorLayer::RefreshModelList()
    {
        m_AvailableModels.clear();
        std::string rootPath = Config::ASSET_DIR + "models";
        if (!std::filesystem::exists(rootPath)) return;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(rootPath))
        {
            if (entry.is_regular_file() && (entry.path().extension() == ".gltf" || entry.path().extension() == ".glb" || entry.path().extension() == ".obj"))
            {
                m_AvailableModels.push_back({ entry.path().filename().string(), std::filesystem::relative(entry.path(), ".").generic_string() });
            }
        }
    }

    void EditorLayer::LoadModel(const std::string& path)
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
                if (ImGui::MenuItem("Refresh Assets")) RefreshModelList();
                if (ImGui::MenuItem("Exit")) Application::Get().Close();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View"))
            {
                ImGui::MenuItem("Control Panel", nullptr, &m_ShowControlPanel);
                ImGui::MenuItem("Viewport", nullptr, &m_ShowViewport);
                ImGui::Separator();
                if (ImGui::MenuItem("Reset Layout")) ImGui::GetIO().IniFilename = nullptr;
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    }

    void EditorLayer::DrawLightSettings(RenderPath* activePath)
    {
        Scene* scene = GetActiveSceneRaw();
        if (!scene) return;
        auto& light = scene->GetLight();
        bool changed = false;
        if (ImGui::TreeNodeEx("Directional Light", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::DragFloat3("Direction", &light.direction.x, 0.01f, -1.0f, 1.0f))
            {
                light.direction = glm::vec4(glm::normalize(glm::vec3(light.direction)), 0.0f);
                changed = true;
            }
            if (ImGui::ColorEdit3("Color", &light.color.x)) changed = true;
            if (ImGui::DragFloat("Intensity", &light.intensity.x, 0.1f, 0.0f, 100.0f)) changed = true;

            if (activePath && activePath->GetType() != RenderPathType::Forward)
            {
                if (ImGui::SliderFloat("Light Radius", &m_LightRadius, 0.0f, 0.5f))
                {
                    light.intensity.y = m_LightRadius;
                    changed = true;
                }
            }
            ImGui::TreePop();
        }
        if (changed && activePath) activePath->OnSceneUpdated();
    }

    void EditorLayer::DrawRenderPathPanel(RenderPath* activePath)
    {
        const auto& allTypes = GetAllRenderPathTypes();
        RenderPathType currentType = activePath ? activePath->GetType() : RenderPathType::Forward;
        ImGui::Text("Active Path:");
        ImGui::SameLine();
        if (ImGui::BeginCombo("##ActivePath", RenderPathTypeToString(currentType)))
        {
            for (auto type : allTypes)
            {
                if (ImGui::Selectable(RenderPathTypeToString(type), currentType == type)) SwitchRenderPath(type);
            }
            ImGui::EndCombo();
        }
        ImGui::Separator();
        ImGui::SliderFloat("Exposure", &m_Exposure, 0.01f, 10.0f);
        
        const char* displayModes[] = { "Final Color", "Albedo", "Normal", "Material", "Motion", "Depth", "Shadow/AO", "Reflection", "Diffuse GI" };
        int currentDisplayMode = (int)m_DisplayMode;
        if (ImGui::Combo("Display Mode", &currentDisplayMode, displayModes, IM_ARRAYSIZE(displayModes))) m_DisplayMode = (uint32_t)currentDisplayMode;

        ImGui::Separator();

        if (currentType == RenderPathType::Hybrid)
        {
            if (ImGui::TreeNodeEx("Ray Tracing Features", ImGuiTreeNodeFlags_DefaultOpen))
            {
                bool shadow = (m_RenderFlags & RENDER_FLAG_SHADOW_BIT) != 0;
                if (ImGui::Checkbox("RT Shadows", &shadow)) m_RenderFlags ^= RENDER_FLAG_SHADOW_BIT;

                bool refl = (m_RenderFlags & RENDER_FLAG_REFLECTION_BIT) != 0;
                if (ImGui::Checkbox("RT Reflections", &refl)) m_RenderFlags ^= RENDER_FLAG_REFLECTION_BIT;

                bool gi = (m_RenderFlags & RENDER_FLAG_GI_BIT) != 0;
                if (ImGui::Checkbox("RT Diffuse GI", &gi)) m_RenderFlags ^= RENDER_FLAG_GI_BIT;

                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx("Denoising & AA", ImGuiTreeNodeFlags_DefaultOpen))
            {
                bool svgf = (m_RenderFlags & RENDER_FLAG_SVGF_BIT) != 0;
                if (ImGui::Checkbox("Enable SVGF", &svgf))
                {
                    if (svgf) m_RenderFlags |= RENDER_FLAG_SVGF_BIT;
                    else      m_RenderFlags &= ~RENDER_FLAG_SVGF_BIT;
                }
                
                if (svgf)
                {
                    ImGui::Indent();
                    ImGui::SliderFloat("Color Alpha", &m_SVGFAlphaColor, 0.01f, 1.0f);
                    ImGui::SliderFloat("Moments Alpha", &m_SVGFAlphaMoments, 0.01f, 1.0f);
                    ImGui::SliderFloat("Phi Color", &m_SVGFPhiColor, 0.1f, 50.0f);
                    ImGui::SliderFloat("Phi Normal", &m_SVGFPhiNormal, 1.0f, 256.0f);
                    ImGui::SliderFloat("Phi Depth", &m_SVGFPhiDepth, 0.01f, 1.0f);
                    ImGui::Unindent();
                }

                bool taa = (m_RenderFlags & RENDER_FLAG_TAA_BIT) != 0;
                if (ImGui::Checkbox("Enable TAA", &taa)) m_RenderFlags ^= RENDER_FLAG_TAA_BIT;

                bool var = (m_RenderFlags & RENDER_FLAG_SHOW_VARIANCE) != 0;
                if (ImGui::Checkbox("Show Denoising Variance", &var)) m_RenderFlags ^= RENDER_FLAG_SHOW_VARIANCE;

                ImGui::TreePop();
            }
        }
        if (activePath) activePath->OnImGui();
    }

    void EditorLayer::DrawControlPanelContent(RenderPath* activePath)
    {
        if (ImGui::CollapsingHeader("1. Pipeline & Display", ImGuiTreeNodeFlags_DefaultOpen)) DrawRenderPathPanel(activePath);
        if (ImGui::CollapsingHeader("General Settings")) DrawGeneralSettings();
        if (ImGui::CollapsingHeader("2. Lighting & Environment", ImGuiTreeNodeFlags_DefaultOpen)) DrawLightSettings(activePath);
        if (ImGui::CollapsingHeader("3. Scene Management", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::TreeNodeEx("Hierarchy", ImGuiTreeNodeFlags_DefaultOpen))
            {
                DrawSceneHierarchy();
                ImGui::TreePop();
            }
            ImGui::Separator();
            if (ImGui::TreeNodeEx("Properties", ImGuiTreeNodeFlags_DefaultOpen))
            {
                DrawPropertiesPanel(activePath);
                ImGui::TreePop();
            }
        }
        if (ImGui::CollapsingHeader("4. Asset Library"))
        {
            if (ImGui::Button("Refresh List")) RefreshModelList();
            ImGui::BeginChild("ModelList", ImVec2(0, 200), true);
            for (int i = 0; i < (int)m_AvailableModels.size(); i++)
            {
                ImGui::PushID(i);
                if (ImGui::Selectable(m_AvailableModels[i].Name.c_str(), m_SelectedModelIndex == i))
                {
                    m_SelectedModelIndex = i;
                    LoadModel(m_AvailableModels[i].Path);
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
        if (ImGui::CollapsingHeader("5. Camera"))
        {
            if (ImGui::Button("Reset Camera")) m_EditorCamera.Reset();
            float d = m_EditorCamera.GetDistance();
            if (ImGui::DragFloat("Distance", &d, 0.1f, 0.1f, 1000.0f)) m_EditorCamera.SetDistance(d);
            float n = m_EditorCamera.GetNearClip();
            if (ImGui::DragFloat("Near Clip", &n, 0.01f, 0.001f, 10.0f)) m_EditorCamera.SetNearClip(n);
            float f = m_EditorCamera.GetFarClip();
            if (ImGui::DragFloat("Far Clip", &f, 1.0f, 1.0f, 10000.0f)) m_EditorCamera.SetFarClip(f);
            glm::vec3 fp = m_EditorCamera.GetFocalPoint();
            if (ImGui::DragFloat3("Focal Point", &fp.x, 0.1f)) m_EditorCamera.SetFocalPoint(fp);
        }
        if (ImGui::CollapsingHeader("6. System & Statistics", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Columns(2, nullptr, false);
            ImGui::Text("Frame Time:"); ImGui::NextColumn(); ImGui::TextColored(ImVec4(0, 1, 0.5f, 1), "%.3f ms", m_AverageFrameTime); ImGui::NextColumn();
            ImGui::Text("FPS:"); ImGui::NextColumn(); ImGui::TextColored(ImVec4(0, 1, 0.5f, 1), "%.1f", m_AverageFPS); ImGui::Columns(1);
            ImGui::Separator();
            if (activePath)
            {
                ImGui::Text("GPU Breakdown:");
                activePath->GetRenderGraph().DrawPerformanceStatistics();
            }
            ImGui::Separator();
            if (ImGui::Button("Export Graph (Mermaid)", ImVec2(-1, 0)) && activePath) ImGui::SetClipboardText(activePath->GetRenderGraph().ExportToMermaid().c_str());
        }
    }

    void EditorLayer::DrawGeneralSettings()
    {
        ImGui::Text("Global Background Color:");
        ImGui::ColorEdit4("Clear Color", &m_ClearColor.x);
    }

    void EditorLayer::DrawViewportContent(RenderPath* activePath)
    {
        m_ViewportHovered = ImGui::IsWindowHovered();
        m_ViewportFocused = ImGui::IsWindowFocused();
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float targetAspect = 16.0f / 9.0f;
        ImVec2 renderSize = (avail.x / avail.y > targetAspect) ? ImVec2(avail.y * targetAspect, avail.y) : ImVec2(avail.x, avail.x / targetAspect);
        if (renderSize.x > 0 && (std::abs(renderSize.x - m_ViewportSize.x) > 0.1f))
        {
            m_NextViewportSize = glm::vec2(renderSize.x, renderSize.y);
            m_ResizeTimer = 0.05f;
        }

        if (activePath)
        {
            auto& graph = activePath->GetRenderGraph();
            std::string tex = "";
            if (graph.ContainsImage("TAAOutput")) tex = "TAAOutput";
            else if (graph.ContainsImage(RS::FinalColor)) tex = RS::FinalColor;
            else tex = RS::RENDER_OUTPUT;

            if (graph.ContainsImage(tex))
            {
                auto& img = graph.GetImage(tex);
                if (img.handle != VK_NULL_HANDLE)
                {
                    ImTextureID id = Application::Get().GetImGuiLayer()->GetTextureID(img.debug_view ? img.debug_view : img.view, ResourceManager::Get().GetDefaultSampler());
                    if (id)
                    {
                        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPos().x + (avail.x - renderSize.x) * 0.5f, ImGui::GetCursorPos().y + (avail.y - renderSize.y) * 0.5f));
                        ImGui::Image(id, renderSize);
                    }
                }
            }
        }
    }
}
