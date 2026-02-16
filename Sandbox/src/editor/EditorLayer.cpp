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

namespace Chimera {

    EditorLayer::EditorLayer()
        : Layer("EditorLayer"), m_EditorCamera(45.0f, 1.778f, 0.1f, 1000.0f)
    {
        m_ShowControlPanel = true;
        m_ShowViewport = true;

        auto& app = Application::Get();
        m_ViewportSize = { (float)app.GetWindow().GetWidth(), (float)app.GetWindow().GetHeight() };

        m_EditorCamera.SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
        m_EditorCamera.SetFocalPoint({ 0.0f, 1.0f, 0.0f });
        m_EditorCamera.SetDistance(5.0f);

        m_Scene = std::make_shared<Scene>(app.GetContext());
        SwitchRenderPath(RenderPathType::Hybrid);
    }

    void EditorLayer::OnAttach()
    {
        RefreshModelList();
        RefreshSkyboxList();
        LoadScene(Config::ASSET_DIR + "models/fantasy_queen/scene.gltf");
        LoadSkybox(Config::ASSET_DIR + "textures/newport_loft.hdr");
    }

    void EditorLayer::RefreshSkyboxList()
    {
        m_AvailableSkyboxes.clear();
        std::string rootPath = Config::ASSET_DIR + "textures";
        if (!std::filesystem::exists(rootPath))
        {
            return;
        }

        for (const auto& entry : std::filesystem::directory_iterator(rootPath))
        {
            if (entry.is_regular_file())
            {
                auto ext = entry.path().extension().string();
                if (ext == ".hdr")
                {
                    std::string relPath = std::filesystem::relative(entry.path(), ".").string();
                    std::replace(relPath.begin(), relPath.end(), '\\', '/');
                    m_AvailableSkyboxes.push_back(relPath);
                }
            }
        }
    }

    void EditorLayer::OnDetach() {}

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

                Application::Get().QueueEvent([this, w, h]() {
                    vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
                    
                    // Update Renderer first to ensure swapchain is ready
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
        context.DisplayMode = m_DisplayMode;
        context.RenderFlags = m_RenderFlags;
        context.Exposure = m_Exposure;
        context.AmbientStrength = m_AmbientStrength;
        context.BloomStrength = m_BloomStrength;
        
        context.SVGFAlphaColor = m_SVGFAlphaColor;
        context.SVGFAlphaMoments = m_SVGFAlphaMoments;
        context.SVGFPhiColor = m_SVGFPhiColor;
        context.SVGFPhiNormal = m_SVGFPhiNormal;
        context.SVGFPhiDepth = m_SVGFPhiDepth;
        context.LightRadius = m_LightRadius;

        // --- TAA Jitter Implementation ---
        static const float haltonX[] = { 0.5f, 0.25f, 0.75f, 0.125f, 0.625f, 0.375f, 0.875f, 0.0625f };
        static const float haltonY[] = { 0.333f, 0.666f, 0.111f, 0.444f, 0.777f, 0.222f, 0.555f, 0.888f };
        
        bool taaEnabled = (m_RenderFlags & 2) != 0;
        if (taaEnabled)
        {
            uint32_t jitterIdx = Application::Get().GetTotalFrameCount() % 8;
            glm::vec2 jitter = { (haltonX[jitterIdx] - 0.5f) / m_ViewportSize.x, (haltonY[jitterIdx] - 0.5f) / m_ViewportSize.y };
            
            // Apply jitter to projection matrix
            glm::mat4 jitterMat = glm::translate(glm::mat4(1.0f), glm::vec3(jitter.x, jitter.y, 0.0f));
            context.Projection = jitterMat * context.Projection;
        }

        Application::Get().SetFrameContext(context);
        Application::Get().SetActiveScene(m_Scene.get());

        // Keyboard Shortcuts
        if (Input::IsKeyPressed(KeyCode::Space))
        {
            m_RenderFlags ^= 1; // Toggle SVGF Bit
        }
        
        if (GetRenderPath() && Renderer::HasInstance())
        {
            if (Renderer::Get().IsFrameInProgress())
            {
                RenderFrameInfo frameInfo{};
                frameInfo.commandBuffer = Renderer::Get().GetActiveCommandBuffer();
                frameInfo.frameIndex = Renderer::Get().GetCurrentFrameIndex();
                frameInfo.imageIndex = Renderer::Get().GetCurrentImageIndex();
                frameInfo.globalSet = Application::Get().GetRenderState()->GetDescriptorSet(frameInfo.frameIndex);
                
                GetRenderPath()->Render(frameInfo);
            }
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
                auto newPath = RenderPathFactory::Create(type, Application::Get().GetContext(), m_Scene);
                if (newPath)
                {
                    newPath->SetViewportSize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
                    // Sync with application
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
            m_Scene->LoadModel(path);
            if (GetRenderPath())
            {
                GetRenderPath()->OnSceneUpdated();
            }
        });
    }

    void EditorLayer::ClearScene()
    {
        Application::Get().QueueEvent([this]()
        {
            vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
            m_Scene = std::make_shared<Scene>(Application::Get().GetContext());
            if (GetRenderPath())
            {
                GetRenderPath()->OnSceneUpdated();
            }
        });
    }

    void EditorLayer::LoadSkybox(const std::string& path)
    {
        CH_CORE_INFO("EditorLayer: Requesting Skybox load: {}", path);
        if (!std::filesystem::exists(path)) {
            CH_CORE_ERROR("EditorLayer: Skybox file does not exist at path: {}", path);
            return;
        }

        Application::Get().QueueEvent([this, path]()
        {
            CH_CORE_INFO("EditorLayer: [Event] Executing LoadSkybox for: {}", path);
            vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
            m_Scene->LoadSkybox(path);
            
            // [FIX] Force update scene descriptor set immediately
            ResourceManager::Get().UpdateSceneDescriptorSet(m_Scene.get());

            if (GetRenderPath())
            {
                GetRenderPath()->OnSceneUpdated();
            }
            CH_CORE_INFO("EditorLayer: [Event] Skybox load completed and Descriptor Set updated.");
        });
    }

    void EditorLayer::ClearSkybox()
    {
        Application::Get().QueueEvent([this]()
        {
            vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
            m_Scene->ClearSkybox();
            ResourceManager::Get().UpdateSceneDescriptorSet(m_Scene.get());
            if (GetRenderPath())
            {
                GetRenderPath()->OnSceneUpdated();
            }
            CH_CORE_INFO("EditorLayer: Skybox cleared.");
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
        static bool resetRequested = false;
        if (ImGui::GetIO().IniFilename == nullptr) resetRequested = true;

        if (!ImGui::DockBuilderGetNode(dockspace_id) || resetRequested)
        {
            if (resetRequested) ImGui::GetIO().IniFilename = "imgui.ini";
            resetRequested = false;
            ImGui::DockBuilderRemoveNode(dockspace_id);
            ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);
            ImGuiID dock_main_id = dockspace_id;
            ImGuiID dock_bottom_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.3f, nullptr, &dock_main_id);
            ImGui::DockBuilderDockWindow("Viewport", dock_main_id);
            ImGui::DockBuilderDockWindow("Control Panel", dock_bottom_id);
            ImGui::DockBuilderFinish(dockspace_id);
        }
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

        DrawMenuBar();
        
        ImGui::Begin("Viewport", &m_ShowViewport);
        DrawViewportContent(); 
        ImGui::End();

        ImGui::Begin("Control Panel", &m_ShowControlPanel);
        DrawControlPanelContent();
        ImGui::End();

        ImGui::End(); // End DockSpaceParent
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
            if (changed) {
                m_Scene->UpdateEntityTRS(m_SelectedInstanceIndex, pos, rot, scale); 
                if (GetRenderPath()) GetRenderPath()->OnSceneUpdated();
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
                if (ext == ".gltf" || ext == ".glb" || ext == ".obj") {
                    std::string relPath = std::filesystem::relative(entry.path(), ".").string();
                    std::replace(relPath.begin(), relPath.end(), '\\', '/');
                    m_AvailableModels.push_back({ entry.path().filename().string(), relPath });
                }
            }
        }
    }

    void EditorLayer::LoadModel(const std::string& path) { m_ActiveModelPath = path; LoadScene(path); }

    void EditorLayer::DrawMenuBar()
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Refresh Assets")) RefreshModelList();
                if (ImGui::MenuItem("Exit")) Application::Get().Close(); 
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Control Panel", nullptr, &m_ShowControlPanel);
                ImGui::MenuItem("Viewport", nullptr, &m_ShowViewport);
                ImGui::Separator();
                if (ImGui::MenuItem("Reset Layout")) { ImGui::GetIO().IniFilename = nullptr; }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    }

    void EditorLayer::DrawLightSettings()
    {
        if (!m_Scene) return;
        auto& light = m_Scene->GetLight(); bool changed = false;
        if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            glm::vec3 dir = glm::vec3(light.direction);
            if (ImGui::DragFloat3("Direction", &dir.x, 0.01f, -1.0f, 1.0f)) { light.direction = glm::vec4(glm::normalize(dir), 0.0f); changed = true; }
            glm::vec3 color = glm::vec3(light.color);
            if (ImGui::ColorEdit3("Color", &color.x)) { light.color = glm::vec4(color, 1.0f); changed = true; }
            float intensity = light.intensity.x;
            if (ImGui::DragFloat("Intensity", &intensity, 0.1f, 0.0f, 100.0f)) { light.intensity = glm::vec4(intensity); changed = true; }
        }
        
        if (ImGui::CollapsingHeader("Environment (IBL)", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SliderFloat("Ambient Strength", &m_AmbientStrength, 0.0f, 5.0f);
            
            if (ImGui::Button("Refresh Skyboxes"))
            {
                RefreshSkyboxList();
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear Skybox"))
            {
                ClearSkybox();
            }

            const char* currentSkybox = m_AvailableSkyboxes.empty() ? "None" : m_AvailableSkyboxes[m_SelectedSkyboxIndex].c_str();
            if (ImGui::BeginCombo("Environment Map", currentSkybox))
            {
                for (int i = 0; i < (int)m_AvailableSkyboxes.size(); i++)
                {
                    bool isSelected = (m_SelectedSkyboxIndex == i);
                    if (ImGui::Selectable(m_AvailableSkyboxes[i].c_str(), isSelected))
                    {
                        m_SelectedSkyboxIndex = i;
                        LoadSkybox(m_AvailableSkyboxes[i]);
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        if (changed && GetRenderPath()) GetRenderPath()->OnSceneUpdated();
    }

    void EditorLayer::DrawRenderPathPanel()
    {
        const auto& allTypes = GetAllRenderPathTypes();
        RenderPathType currentType = GetRenderPath() ? GetRenderPath()->GetType() : RenderPathType::Forward;
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
        if (GetRenderPath())
        {
            std::vector<std::string> attachments = GetRenderPath()->GetRenderGraph().GetColorAttachments();
            const char* preview = m_DebugViewTexture.empty() ? "None (Final)" : m_DebugViewTexture.c_str();
            
            if (ImGui::BeginCombo("Debug Image", preview))
            {
                if (ImGui::Selectable("None (Final Output)", m_DebugViewTexture.empty()))
                {
                    m_DebugViewTexture = "";
                }

                for (const auto& name : attachments)
                {
                    bool isSelected = (m_DebugViewTexture == name);
                    if (ImGui::Selectable(name.c_str(), isSelected))
                    {
                        m_DebugViewTexture = name;
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }
        ImGui::Separator();
        DrawLightSettings();
        ImGui::Separator();

        if (ImGui::CollapsingHeader("Debug & Denoising", ImGuiTreeNodeFlags_DefaultOpen))
        {
            bool svgfEnabled = (m_RenderFlags & 1) != 0;
            if (ImGui::Checkbox("SVGF Denoising", &svgfEnabled))
            {
                if (svgfEnabled) m_RenderFlags |= 1;
                else m_RenderFlags &= ~1;
            }

            bool taaEnabled = (m_RenderFlags & 2) != 0;
            if (ImGui::Checkbox("Temporal Anti-Aliasing (TAA)", &taaEnabled))
            {
                if (taaEnabled) m_RenderFlags |= 2;
                else m_RenderFlags &= ~2;
            }

            const char* displayModes[] = { "Final", "Albedo", "Normal", "Material", "Motion", "Depth", "ShadowAO", "Reflection" };
            if (ImGui::Combo("Display Mode", (int*)&m_DisplayMode, displayModes, IM_ARRAYSIZE(displayModes)))
            {
                // Mode changed
            }

            ImGui::SliderFloat("Exposure", &m_Exposure, 0.01f, 10.0f);
            ImGui::SliderFloat("Bloom Strength", &m_BloomStrength, 0.0f, 2.0f);

            ImGui::Separator();
            ImGui::Text("SVGF Parameters");
            ImGui::SliderFloat("Alpha Color", &m_SVGFAlphaColor, 0.01f, 1.0f);
            ImGui::SliderFloat("Alpha Moments", &m_SVGFAlphaMoments, 0.01f, 1.0f);
            ImGui::SliderFloat("Phi Color", &m_SVGFPhiColor, 0.1f, 50.0f);
            ImGui::SliderFloat("Phi Normal", &m_SVGFPhiNormal, 1.0f, 256.0f);
            ImGui::SliderFloat("Phi Depth", &m_SVGFPhiDepth, 0.01f, 1.0f);

            ImGui::Separator();
            ImGui::Text("Light Parameters");
            ImGui::SliderFloat("Light Radius", &m_LightRadius, 0.0f, 0.5f);

            ImGui::Separator();
            if (ImGui::Button("Export RenderGraph (Mermaid)"))
            {
                if (GetRenderPath())
                {
                    std::string mermaid = GetRenderPath()->GetRenderGraph().ExportToMermaid();
                    ImGui::SetClipboardText(mermaid.c_str());
                    CH_CORE_INFO("EditorLayer: Mermaid code copied to clipboard!\n{}", mermaid);
                }
            }

            if (ImGui::Button("Take Screenshot"))
            {
                auto swapchain = Application::Get().GetContext()->GetSwapchain();
                if (swapchain)
                {
                    if (!std::filesystem::exists("screenshots"))
                    {
                        std::filesystem::create_directory("screenshots");
                    }

                    std::string filename = "screenshots/chimera_" + std::to_string(std::time(nullptr)) + ".ppm";
                    VulkanScreenshot::SaveToPPM(
                        swapchain->GetImages()[0], 
                        swapchain->GetFormat(), 
                        swapchain->GetExtent(), 
                        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 
                        filename
                    );
                    CH_CORE_INFO("EditorLayer: Screenshot saved to: {}", filename);
                }
            }

            if (ImGui::Button("Capture Viewport (No UI)"))
            {
                if (GetRenderPath())
                {
                    auto& graph = GetRenderPath()->GetRenderGraph();
                    // Prefer TAA Output, fallback to FinalColor
                    std::string targetRes = "TAAOutput";
                    if (!graph.ContainsImage(targetRes))
                    {
                        targetRes = RS::FinalColor;
                    }

                    if (graph.ContainsImage(targetRes))
                    {
                        auto& img = graph.GetImage(targetRes);
                        if (img.handle != VK_NULL_HANDLE)
                        {
                            if (!std::filesystem::exists("screenshots"))
                            {
                                std::filesystem::create_directory("screenshots");
                            }

                            std::string filename = "screenshots/viewport_" + std::to_string(std::time(nullptr)) + ".ppm";
                            
                            // Internal graph images are transitioned to SHADER_READ_ONLY_OPTIMAL at the end of Execute
                            VulkanScreenshot::SaveToPPM(
                                img.handle,
                                img.format,
                                { img.width, img.height },
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                filename
                            );
                            CH_CORE_INFO("EditorLayer: Viewport screenshot saved to: {}", filename);
                        }
                    }
                }
            }
        }

        ImGui::Separator();
        if (GetRenderPath())
        {
            GetRenderPath()->OnImGui();
        }
    }

    void EditorLayer::DrawModelSelectionPanel()
    {
        if (ImGui::Button("Refresh List")) RefreshModelList();
        ImGui::BeginChild("ModelList", ImVec2(0, 200), true);
        for (int i = 0; i < (int)m_AvailableModels.size(); i++) {
            ImGui::PushID(i);
            if (ImGui::Selectable(m_AvailableModels[i].Name.c_str(), m_SelectedModelIndex == i)) { m_SelectedModelIndex = i; LoadModel(m_AvailableModels[i].Path); }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    void EditorLayer::DrawControlPanelContent()
    {
        if (ImGui::CollapsingHeader("Renderer Settings", ImGuiTreeNodeFlags_DefaultOpen)) DrawRenderPathPanel();
        if (ImGui::CollapsingHeader("Scene Hierarchy", ImGuiTreeNodeFlags_DefaultOpen)) DrawSceneHierarchy();
        if (ImGui::CollapsingHeader("Properties", ImGuiTreeNodeFlags_DefaultOpen)) DrawPropertiesPanel();
        if (ImGui::CollapsingHeader("Asset Library")) DrawModelSelectionPanel();
        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::Button("Reset Camera")) m_EditorCamera.Reset();
            float distance = m_EditorCamera.GetDistance();
            if (ImGui::DragFloat("Distance", &distance, 0.1f, 0.1f, 1000.0f)) m_EditorCamera.SetDistance(distance);
            float nearClip = m_EditorCamera.GetNearClip();
            if (ImGui::DragFloat("Near Clip", &nearClip, 0.01f, 0.001f, 10.0f)) m_EditorCamera.SetNearClip(nearClip);
            float farClip = m_EditorCamera.GetFarClip();
            if (ImGui::DragFloat("Far Clip", &farClip, 1.0f, 1.0f, 10000.0f)) m_EditorCamera.SetFarClip(farClip);
            glm::vec3 focalPoint = m_EditorCamera.GetFocalPoint();
            if (ImGui::DragFloat3("Focal Point", &focalPoint.x, 0.1f)) m_EditorCamera.SetFocalPoint(focalPoint);
        }
        if (ImGui::CollapsingHeader("Statistics", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Frame Time: %.3f ms", m_AverageFrameTime);
            ImGui::Text("FPS: %.1f", m_AverageFPS);
            if (GetRenderPath())
            {
                GetRenderPath()->GetRenderGraph().DrawPerformanceStatistics();
            }
        }
    }

    void EditorLayer::DrawViewportContent()
    {
        m_ViewportHovered = ImGui::IsWindowHovered();
        m_ViewportFocused = ImGui::IsWindowFocused();
        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        if (viewportPanelSize.x > 0 && viewportPanelSize.y > 0 && (std::abs(viewportPanelSize.x - m_ViewportSize.x) > 0.1f || std::abs(viewportPanelSize.y - m_ViewportSize.y) > 0.1f)) {
            m_NextViewportSize = glm::vec2(viewportPanelSize.x, viewportPanelSize.y);
            m_ResizeTimer = 0.05f;
        }
        if (GetRenderPath())
        {
            std::string textureToDisplay = m_DebugViewTexture;
            if (!GetRenderPath()->GetRenderGraph().ContainsImage(textureToDisplay))
            {
                textureToDisplay = RS::Albedo;
            }
            if (GetRenderPath()->GetRenderGraph().ContainsImage(textureToDisplay))
            {
                auto& img = GetRenderPath()->GetRenderGraph().GetImage(textureToDisplay);
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
    }
}
