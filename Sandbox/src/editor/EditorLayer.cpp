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
#include <filesystem>
#include "Renderer/Pipelines/RenderPathFactory.h"
#include "Renderer/RenderState.h"

namespace Chimera {

    EditorLayer::EditorLayer()
        : Layer("EditorLayer"), m_EditorCamera(45.0f, 1.778f, 0.1f, 1000.0f)
    {
        auto& app = Application::Get();
        m_ViewportSize = { (float)app.GetWindow().GetWidth(), (float)app.GetWindow().GetHeight() };

        m_EditorCamera.SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
        m_EditorCamera.SetFocalPoint({ 0.0f, 1.0f, 0.0f });
        m_EditorCamera.SetDistance(5.0f);

        // Initialize Scene and default RenderPath
        m_Scene = std::make_shared<Scene>(app.GetContext());
        SwitchRenderPath(RenderPathType::Hybrid);
    }

    void EditorLayer::OnAttach()
    {
        CH_CORE_INFO("EditorLayer: OnAttach starting...");
        RefreshModelList();

        // Standard way to load default scene
        LoadScene(Config::ASSET_DIR + "models/fantasy_queen/scene.gltf");
        CH_CORE_INFO("EditorLayer: OnAttach complete.");
    }

    void EditorLayer::OnDetach()
    {
    }

    void EditorLayer::OnUpdate(Timestep ts)
    {
        // 1. Update logic
        m_AverageFrameTime = ts.GetMilliseconds();
        m_AverageFPS = 1.0f / ts.GetSeconds();

        // Handle Resize Debounce
        if (m_ResizeTimer > 0.0f)
        {
            m_ResizeTimer -= ts.GetSeconds();
            if (m_ResizeTimer <= 0.0f && !m_ResizePending)
            {
                CH_CORE_INFO("EditorLayer: Resize settled. Rebuilding RenderPath to {0}x{1}", (uint32_t)m_NextViewportSize.x, (uint32_t)m_NextViewportSize.y);
                
                uint32_t w = (uint32_t)m_NextViewportSize.x;
                uint32_t h = (uint32_t)m_NextViewportSize.y;
                m_ResizePending = true;

                Application::Get().QueueEvent([this, w, h]() {
                    vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
                    if (m_RenderPath)
                        m_RenderPath->SetViewportSize(w, h);
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
        Application::Get().SetFrameContext(context);
        Application::Get().SetActiveScene(m_Scene.get());

        if (m_RenderPath && Renderer::HasInstance())
        {
            if (Renderer::Get().IsFrameInProgress())
            {
                RenderFrameInfo frameInfo{};
                frameInfo.commandBuffer = Renderer::Get().GetActiveCommandBuffer();
                frameInfo.frameIndex = Renderer::Get().GetCurrentFrameIndex();
                frameInfo.imageIndex = Renderer::Get().GetCurrentImageIndex();
                frameInfo.globalSet = Application::Get().GetRenderState()->GetDescriptorSet(frameInfo.frameIndex);

                m_RenderPath->Render(frameInfo);
            }
        }
    }

    void EditorLayer::OnEvent(Event& e)
    {
        m_EditorCamera.OnEvent(e);
    }

    void EditorLayer::SwitchRenderPath(RenderPathType type)
    {
        Application::Get().QueueEvent([this, type]()
        {
            CH_CORE_INFO("EditorLayer: Switching Render Path to {0}...", (int)type);
            vkDeviceWaitIdle(VulkanContext::Get().GetDevice());

            m_RenderPath.reset();

            try
            {
                m_RenderPath = RenderPathFactory::Create(type, Application::Get().GetContext(), m_Scene);

                if (m_RenderPath)
                {
                    m_RenderPath->SetViewportSize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
                }
                CH_CORE_INFO("EditorLayer: Render Path switch complete.");
            }
            catch (const std::exception& e)
            {
                CH_CORE_ERROR("EditorLayer: Failed to switch render path: {0}", e.what());
            }
        });
    }

    void EditorLayer::LoadScene(const std::string& path)
    {
        Application::Get().QueueEvent([this, path]()
        {
            CH_CORE_INFO("EditorLayer: [Event] LoadScene starting: {0}", path);
            vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
            m_Scene->LoadModel(path);

            if (m_RenderPath)
            {
                CH_CORE_INFO("EditorLayer: [Event] Notifying RenderPath of scene update...");
                m_RenderPath->OnSceneUpdated();
            }
            CH_CORE_INFO("EditorLayer: [Event] LoadScene complete.");
        });
    }

    void EditorLayer::ClearScene()
    {
        Application::Get().QueueEvent([this]()
        {
            vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
            m_Scene = std::make_shared<Scene>(Application::Get().GetContext());

            if (m_RenderPath)
            {
                m_RenderPath->OnSceneUpdated();
            }
        });
    }

    void EditorLayer::LoadSkybox(const std::string& path)
    {
        Application::Get().QueueEvent([this, path]()
        {
            CH_CORE_INFO("EditorLayer: [Event] LoadSkybox starting: {0}", path);
            vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
            m_Scene->LoadSkybox(path);
            CH_CORE_INFO("EditorLayer: [Event] LoadSkybox complete.");
        });
    }

    void EditorLayer::OnImGuiRender()
    {
        static bool dockingEnabled = true;
        if (dockingEnabled)
        {
            static bool dockspaceOpen = true;
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->Pos);
            ImGui::SetNextWindowSize(viewport->Size);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::Begin("DockSpaceParent", &dockspaceOpen, window_flags);
            ImGui::PopStyleVar(3);

            ImGuiIO& io = ImGui::GetIO();
            if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
            {
                ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
                ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
            }

            DrawMenuBar();
            DrawViewport();

            ImGui::Begin("Settings");
            if (ImGui::BeginTabBar("SettingsTabs"))
            {
                if (ImGui::BeginTabItem("Renderer"))
                {
                    DrawRenderPathPanel();
                    ImGui::Separator();
                    ImGui::Text("Scene Hierarchy");
                    DrawSceneHierarchy();

                    ImGui::Separator();
                    DrawPropertiesPanel();

                    ImGui::Separator();
                    ImGui::Text("Asset Selection");
                    DrawModelSelectionPanel();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Stats"))
                {
                    DrawStatsPanel();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::End();

            ImGui::End(); // DockSpaceParent
        }
    }

    void EditorLayer::DrawSceneHierarchy()
    {
        if (!m_Scene) return;
        if (ImGui::Button("Clear Scene")) { ClearScene(); m_SelectedInstanceIndex = -1; }
        ImGui::Separator();
        const auto& entities = m_Scene->GetEntities();
        for (int i = 0; i < (int)entities.size(); i++) {
            ImGui::PushID(i);
            bool selected = (m_SelectedInstanceIndex == i);
            if (ImGui::Selectable((entities[i].name + "##" + std::to_string(i)).c_str(), selected)) m_SelectedInstanceIndex = i;
            ImGui::PopID();
        }
    }

    void EditorLayer::DrawPropertiesPanel()
    {
        if (!m_Scene || m_SelectedInstanceIndex < 0) { ImGui::Text("Select an object to view properties."); return; }
        const auto& entities = m_Scene->GetEntities();
        if (m_SelectedInstanceIndex >= (int)entities.size()) { m_SelectedInstanceIndex = -1; return; }
        auto& entity = entities[m_SelectedInstanceIndex];
        ImGui::Text("Name: %s", entity.name.c_str());
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool changed = false;
            glm::vec3 pos = entity.transform.position, rot = entity.transform.rotation, scale = entity.transform.scale;
            if (ImGui::DragFloat3("Position", &pos.x, 0.1f)) changed = true;
            if (ImGui::DragFloat3("Rotation", &rot.x, 0.5f)) changed = true;
            if (ImGui::DragFloat3("Scale", &scale.x, 0.05f)) changed = true;
            if (changed) 
            {
                m_Scene->UpdateEntityTRS(m_SelectedInstanceIndex, pos, rot, scale); 
                if (m_RenderPath && m_RenderPath->GetType() == RenderPathType::RayTracing)
                {
                    m_RenderPath->OnSceneUpdated();
                }
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
            if (entry.is_regular_file()) {
                auto ext = entry.path().extension().string();
                if (ext == ".gltf" || ext == ".glb" || ext == ".obj")
                {
                    std::string relPath = std::filesystem::relative(entry.path(), ".").string();
                    std::replace(relPath.begin(), relPath.end(), '\\', '/');
                    m_AvailableModels.push_back({ entry.path().filename().string(), relPath });
                }
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
                if (ImGui::MenuItem("Exit")) Application::Get().Close(); ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    }

    void EditorLayer::DrawStatsPanel()
    {
        if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Frame Time: %.3f ms", m_AverageFrameTime);
            ImGui::Text("FPS: %.1f", m_AverageFPS);
            if (m_RenderPath) m_RenderPath->GetRenderGraph().DrawPerformanceStatistics();
        }
        if (ImGui::CollapsingHeader("Camera Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Button("Reset Camera")) m_EditorCamera.Reset();
            float distance = m_EditorCamera.GetDistance();
            if (ImGui::DragFloat("Distance", &distance, 0.1f, 0.1f, 1000.0f)) m_EditorCamera.SetDistance(distance);
            
            float nearClip = m_EditorCamera.GetNearClip();
            if (ImGui::DragFloat("Near Clip", &nearClip, 0.01f, 0.001f, 10.0f)) {
                m_EditorCamera.SetNearClip(nearClip);
            }
            float farClip = m_EditorCamera.GetFarClip();
            if (ImGui::DragFloat("Far Clip", &farClip, 1.0f, 1.0f, 10000.0f)) {
                m_EditorCamera.SetFarClip(farClip);
            }

            glm::vec3 focalPoint = m_EditorCamera.GetFocalPoint();
            if (ImGui::DragFloat3("Focal Point", &focalPoint.x, 0.1f)) m_EditorCamera.SetFocalPoint(focalPoint);
        }
    }

    void EditorLayer::DrawLightSettings()
    {
        if (!m_Scene)
        {
            return;
        }

        auto& light = m_Scene->GetLight(); bool changed = false;
        if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen))
        {
            glm::vec3 dir = glm::vec3(light.direction);
            if (ImGui::DragFloat3("Direction", &dir.x, 0.01f, -1.0f, 1.0f)) { light.direction = glm::vec4(glm::normalize(dir), 0.0f); changed = true; }
            glm::vec3 color = glm::vec3(light.color);
            if (ImGui::ColorEdit3("Color", &color.x)) { light.color = glm::vec4(color, 1.0f); changed = true; }
            float intensity = light.intensity.x;
            if (ImGui::DragFloat("Intensity", &intensity, 0.1f, 0.0f, 100.0f)) { light.intensity = glm::vec4(intensity); changed = true; }
        }

        if (changed && m_RenderPath && m_RenderPath->GetType() == RenderPathType::RayTracing)
        {
            m_RenderPath->OnSceneUpdated();
        }
    }

    void EditorLayer::DrawRenderPathPanel()
    {
        const auto& allTypes = GetAllRenderPathTypes();
        RenderPathType currentType = m_RenderPath ? m_RenderPath->GetType() : RenderPathType::Forward;
        const char* currentLabel = RenderPathTypeToString(currentType);

        if (ImGui::BeginCombo("Active Path", currentLabel))
        {
            for (auto type : allTypes)
            {
                bool isSelected = (currentType == type);
                if (ImGui::Selectable(RenderPathTypeToString(type), isSelected))
                {
                    SwitchRenderPath(type);
                }

                if (isSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Separator();
        if (m_RenderPath)
        {
            std::vector<std::string> attachments = m_RenderPath->GetRenderGraph().GetColorAttachments();
            if (ImGui::BeginCombo("Debug Image", m_DebugViewTexture.c_str()))
            {
                for (const auto& name : attachments)
                {
                    bool isSelected = (m_DebugViewTexture == name);
                    if (ImGui::Selectable(name.c_str(), isSelected)) m_DebugViewTexture = name;
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }
        ImGui::Separator();

        DrawLightSettings();
        ImGui::Separator();
        if (m_RenderPath) m_RenderPath->OnImGui();
    }

    void EditorLayer::DrawModelSelectionPanel()
    {
        if (ImGui::Button("Refresh List")) RefreshModelList();
        ImGui::BeginChild("ModelList", ImVec2(0, 300), true);
        for (int i = 0; i < (int)m_AvailableModels.size(); i++)
        {
            ImGui::PushID(i);
            if (ImGui::Selectable(m_AvailableModels[i].Name.c_str(), m_SelectedModelIndex == i))
            {
                m_SelectedModelIndex = i; LoadModel(m_AvailableModels[i].Path);
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    void EditorLayer::DrawViewport()
    {
        VkCommandBuffer cmd = Renderer::Get().GetActiveCommandBuffer();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
        ImGui::Begin("Viewport");
        m_ViewportHovered = ImGui::IsWindowHovered();
        m_ViewportFocused = ImGui::IsWindowFocused();
        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        
        // Handle resize with debounce
        if (viewportPanelSize.x > 0 && viewportPanelSize.y > 0 && 
           (std::abs(viewportPanelSize.x - m_ViewportSize.x) > 0.1f || std::abs(viewportPanelSize.y - m_ViewportSize.y) > 0.1f))
        {
            // Just mark for update, don't trigger rebuild yet
            m_NextViewportSize = glm::vec2(viewportPanelSize.x, viewportPanelSize.y);
            m_ResizeTimer = 0.05f; // Wait for 50ms of no changes
        }
        
        if (m_RenderPath)
        {
            std::string textureToDisplay = m_DebugViewTexture;
            if (!m_RenderPath->GetRenderGraph().ContainsImage(textureToDisplay))
            {
                textureToDisplay = RS::Albedo;
            }

            if (m_RenderPath->GetRenderGraph().ContainsImage(textureToDisplay))
            {
                auto& img = m_RenderPath->GetRenderGraph().GetImage(textureToDisplay);
                if (img.handle != VK_NULL_HANDLE)
                {
                    ImTextureID textureID = Application::Get().GetImGuiLayer()->GetTextureID(img.debug_view, ResourceManager::Get().GetDefaultSampler());
                    if (textureID)
                    {
                        ImGui::Image(textureID, viewportPanelSize);
                    }
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }
}
