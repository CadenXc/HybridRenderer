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

        auto scene = std::make_shared<Scene>(app.GetContext());
        ResourceManager::Get().SetActiveScene(scene);

        SwitchRenderPath(RenderPathType::Hybrid);
    }

    void EditorLayer::OnAttach()
    {
        RefreshModelList();
        RefreshSkyboxList();
        LoadScene(Config::ASSET_DIR + "models/pica_pica_-_machines/scene.gltf");
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

        if (m_ResizePending)
        {
            return;
        }
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
        Application::Get().SetActiveScene(GetActiveSceneRaw());

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
        if (m_ViewportHovered)
        {
            m_EditorCamera.OnEvent(e);
        }
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
            GetActiveSceneRaw()->LoadModel(path);
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
            auto scene = std::make_shared<Scene>(Application::Get().GetContext());
            ResourceManager::Get().SetActiveScene(scene);
        });
    }

    void EditorLayer::LoadSkybox(const std::string& path)
    {
        CH_CORE_INFO("EditorLayer: Requesting Skybox load: {}", path);
        if (!std::filesystem::exists(path))
        {
            CH_CORE_ERROR("EditorLayer: Skybox file does not exist at path: {}", path);
            return;
        }

        Application::Get().QueueEvent([this, path]()
        {
            CH_CORE_INFO("EditorLayer: [Event] Executing LoadSkybox for: {}", path);
            vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
            GetActiveSceneRaw()->LoadSkybox(path);
            
            ResourceManager::Get().UpdateSceneDescriptorSet(GetActiveSceneRaw());

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
            GetActiveSceneRaw()->ClearSkybox();
            ResourceManager::Get().UpdateSceneDescriptorSet(GetActiveSceneRaw());
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
        if (ImGui::GetIO().IniFilename == nullptr)
        {
            resetRequested = true;
        }

        if (!ImGui::DockBuilderGetNode(dockspace_id) || resetRequested)
        {
            if (resetRequested)
            {
                ImGui::GetIO().IniFilename = "imgui.ini";
            }
            resetRequested = false;
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
        Scene* scene = GetActiveSceneRaw();
        if (!scene)
        {
            return;
        }
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
            bool selected = (m_SelectedInstanceIndex == i);
            if (ImGui::Selectable((entities[i].name + "##" + std::to_string(i)).c_str(), selected))
            {
                m_SelectedInstanceIndex = i;
            }
            ImGui::PopID();
        }
    }

    void EditorLayer::DrawPropertiesPanel()
    {
        Scene* scene = GetActiveSceneRaw();
        if (!scene || m_SelectedInstanceIndex < 0)
        {
            ImGui::Text("Select an object to view properties.");
            return;
        }
        const auto& entities = scene->GetEntities();
        if (m_SelectedInstanceIndex >= (int)entities.size())
        {
            m_SelectedInstanceIndex = -1;
            return;
        }
        auto& entity = entities[m_SelectedInstanceIndex];
        ImGui::Text("Name: %s", entity.name.c_str());
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            bool changed = false;
            glm::vec3 pos = entity.transform.position, rot = entity.transform.rotation, scale = entity.transform.scale;
            if (ImGui::DragFloat3("Position", &pos.x, 0.1f))
            {
                changed = true;
            }
            if (ImGui::DragFloat3("Rotation", &rot.x, 0.5f))
            {
                changed = true;
            }
            if (ImGui::DragFloat3("Scale", &scale.x, 0.05f))
            {
                changed = true;
            }
            if (changed)
            {
                scene->UpdateEntityTRS(m_SelectedInstanceIndex, pos, rot, scale); 
                if (GetRenderPath())
                {
                    GetRenderPath()->OnSceneUpdated();
                }
            }
        }
    }

    void EditorLayer::RefreshModelList()
    {
        m_AvailableModels.clear();
        std::string rootPath = Config::ASSET_DIR + "models";
        if (!std::filesystem::exists(rootPath))
        {
            return;
        }
        for (const auto& entry : std::filesystem::recursive_directory_iterator(rootPath))
        {
            if (entry.is_regular_file())
            {
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
                if (ImGui::MenuItem("Refresh Assets"))
                {
                    RefreshModelList();
                }
                if (ImGui::MenuItem("Exit"))
                {
                    Application::Get().Close();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View"))
            {
                ImGui::MenuItem("Control Panel", nullptr, &m_ShowControlPanel);
                ImGui::MenuItem("Viewport", nullptr, &m_ShowViewport);
                ImGui::Separator();
                if (ImGui::MenuItem("Reset Layout"))
                {
                    ImGui::GetIO().IniFilename = nullptr;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    }

    void EditorLayer::DrawLightSettings()
    {
        Scene* scene = GetActiveSceneRaw();
        if (!scene)
        {
            return;
        }
        auto& light = scene->GetLight();
        bool changed = false;
        
        if (ImGui::TreeNodeEx("Directional Light", ImGuiTreeNodeFlags_DefaultOpen))
        {
            glm::vec3 dir = glm::vec3(light.direction);
            if (ImGui::DragFloat3("Direction", &dir.x, 0.01f, -1.0f, 1.0f))
            {
                light.direction = glm::vec4(glm::normalize(dir), 0.0f);
                changed = true;
            }
            
            glm::vec3 color = glm::vec3(light.color);
            if (ImGui::ColorEdit3("Color", &color.x))
            {
                light.color = glm::vec4(color, 1.0f);
                changed = true;
            }
            
            float intensity = light.intensity.x;
            if (ImGui::DragFloat("Intensity", &intensity, 0.1f, 0.0f, 100.0f))
            {
                light.intensity.x = intensity;
                changed = true;
            }

            // Only show Light Radius for Ray Tracing based paths (for soft shadows)
            RenderPathType currentType = GetRenderPath() ? GetRenderPath()->GetType() : RenderPathType::Forward;
            if (currentType != RenderPathType::Forward)
            {
                if (ImGui::SliderFloat("Light Radius", &m_LightRadius, 0.0f, 0.5f))
                {
                    // Update intensity.y which stores the radius in our refactored UBO
                    light.intensity.y = m_LightRadius;
                    changed = true;
                }
            }
            
            ImGui::TreePop();
        }
        
        ImGui::Separator();

        if (ImGui::TreeNodeEx("Environment (IBL)", ImGuiTreeNodeFlags_DefaultOpen))
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
                }
                ImGui::EndCombo();
            }
            ImGui::TreePop();
        }

        if (changed && GetRenderPath())
        {
            GetRenderPath()->OnSceneUpdated();
        }
    }

    void EditorLayer::DrawRenderPathPanel()
    {
        const auto& allTypes = GetAllRenderPathTypes();
        RenderPathType currentType = GetRenderPath() ? GetRenderPath()->GetType() : RenderPathType::Forward;
        const char* currentLabel = RenderPathTypeToString(currentType);
        
        ImGui::Text("Active Path:");
        ImGui::SameLine();
        ImGui::Text("Active Path:");
        ImGui::SameLine();
        if (ImGui::BeginCombo("##ActivePath", currentLabel))
        {
            for (auto type : allTypes)
            {
                bool isSelected = (currentType == type);
                if (ImGui::Selectable(RenderPathTypeToString(type), isSelected))
                {
                    SwitchRenderPath(type);
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Separator();

        // --- Display Control ---
        ImGui::SliderFloat("Exposure", &m_Exposure, 0.01f, 10.0f);
        
        // Only show debug images for Hybrid/RayTracing (RenderGraph based paths)
        if (currentType != RenderPathType::Forward && GetRenderPath())
        {
            std::vector<std::string> attachments = GetRenderPath()->GetRenderGraph().GetDebuggableResources();
            const char* preview = m_DebugViewTexture.empty() ? "None (Final Output)" : m_DebugViewTexture.c_str();
            
            if (ImGui::BeginCombo("Debug Texture", preview))
            {
                if (ImGui::Selectable("None (Final Output)", m_DebugViewTexture.empty())) m_DebugViewTexture = "";
                for (const auto& name : attachments)
                {
                    if (ImGui::Selectable(name.c_str(), m_DebugViewTexture == name)) m_DebugViewTexture = name;
                }
                ImGui::EndCombo();
            }
        }

        ImGui::Separator();

        // --- Effects ---
        if (ImGui::TreeNodeEx("Denoising & AA", ImGuiTreeNodeFlags_DefaultOpen))
        {
            bool svgfEnabled = (m_RenderFlags & 1) != 0;
            if (ImGui::Checkbox("Enable SVGF", &svgfEnabled))
            {
                m_RenderFlags ^= 1;
            }

            // Conditional SVGF Sliders
            if (svgfEnabled)
            {
                ImGui::Indent();
                ImGui::SliderFloat("Color Alpha", &m_SVGFAlphaColor, 0.01f, 1.0f);
                ImGui::SliderFloat("Moments Alpha", &m_SVGFAlphaMoments, 0.01f, 1.0f);
                ImGui::SliderFloat("Phi Color", &m_SVGFPhiColor, 0.1f, 50.0f);
                ImGui::SliderFloat("Phi Normal", &m_SVGFPhiNormal, 1.0f, 256.0f);
                ImGui::SliderFloat("Phi Depth", &m_SVGFPhiDepth, 0.01f, 1.0f);
                ImGui::Unindent();
            }

            bool taaEnabled = (m_RenderFlags & 2) != 0;
            if (ImGui::Checkbox("Enable TAA", &taaEnabled))
            {
                m_RenderFlags ^= 2;
            }
            
            ImGui::SliderFloat("Bloom", &m_BloomStrength, 0.0f, 2.0f);
            
            ImGui::TreePop();
        }

        if (GetRenderPath())
        {
            GetRenderPath()->OnImGui();
        }
    }

    void EditorLayer::DrawModelSelectionPanel()
    {
        if (ImGui::Button("Refresh List"))
        {
            RefreshModelList();
        }
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

    void EditorLayer::DrawControlPanelContent()
    {
        // --- Group 1: Pipeline & Display ---
        if (ImGui::CollapsingHeader("1. Pipeline & Display", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawRenderPathPanel();
        }

        // --- Group 2: Lighting & Environment ---
        if (ImGui::CollapsingHeader("2. Lighting & Environment", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawLightSettings();
        }

        // --- Group 3: Scene Management ---
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
                DrawPropertiesPanel();
                ImGui::TreePop();
            }
        }

        // --- Group 4: Assets ---
        if (ImGui::CollapsingHeader("4. Asset Library"))
        {
            DrawModelSelectionPanel();
        }

        // --- Group 5: Camera ---
        if (ImGui::CollapsingHeader("5. Camera"))
        {
            if (ImGui::Button("Reset Camera"))
            {
                m_EditorCamera.Reset();
            }
            float distance = m_EditorCamera.GetDistance();
            if (ImGui::DragFloat("Distance", &distance, 0.1f, 0.1f, 1000.0f))
            {
                m_EditorCamera.SetDistance(distance);
            }
            float nearClip = m_EditorCamera.GetNearClip();
            if (ImGui::DragFloat("Near Clip", &nearClip, 0.01f, 0.001f, 10.0f))
            {
                m_EditorCamera.SetNearClip(nearClip);
            }
            float farClip = m_EditorCamera.GetFarClip();
            if (ImGui::DragFloat("Far Clip", &farClip, 1.0f, 1.0f, 10000.0f))
            {
                m_EditorCamera.SetFarClip(farClip);
            }
            glm::vec3 focalPoint = m_EditorCamera.GetFocalPoint();
            if (ImGui::DragFloat3("Focal Point", &focalPoint.x, 0.1f))
            {
                m_EditorCamera.SetFocalPoint(focalPoint);
            }
        }

        // --- Group 6: System & Stats ---
        if (ImGui::CollapsingHeader("6. System & Statistics", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Columns(2, "StatsColumns", false);
            ImGui::Text("Average Frame:");
            ImGui::NextColumn();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.5f, 1.0f), "%.3f ms", m_AverageFrameTime);
            ImGui::NextColumn();
            
            ImGui::Text("Current FPS:");
            ImGui::NextColumn();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.5f, 1.0f), "%.1f", m_AverageFPS);
            ImGui::Columns(1);

            ImGui::Separator();
            
            if (GetRenderPath())
            {
                ImGui::Text("GPU Workload Breakdown:");
                GetRenderPath()->GetRenderGraph().DrawPerformanceStatistics();
            }

            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.35f, 1.0f));
            
            if (ImGui::Button("Export Graph (Mermaid)", ImVec2(-1, 0)))
            {
                if (GetRenderPath())
                {
                    std::string mermaid = GetRenderPath()->GetRenderGraph().ExportToMermaid();
                    ImGui::SetClipboardText(mermaid.c_str());
                    CH_CORE_INFO("EditorLayer: Mermaid code copied to clipboard!");
                }
            }

            if (ImGui::Button("Save Screenshot", ImVec2(-1, 0)))
            {
                // Trigger screenshot logic
            }
            
            ImGui::PopStyleColor();
        }
    }

    void EditorLayer::DrawViewportContent()
    {
        m_ViewportHovered = ImGui::IsWindowHovered();
        m_ViewportFocused = ImGui::IsWindowFocused();
        
        ImVec2 availableSize = ImGui::GetContentRegionAvail();
        
        // --- Fixed 16:9 Aspect Ratio Logic ---
        float targetAspect = 16.0f / 9.0f;
        float actualAspect = availableSize.x / availableSize.y;
        
        ImVec2 renderSize = availableSize;
        if (actualAspect > targetAspect)
        {
            // Window is too wide, shrink width
            renderSize.x = availableSize.y * targetAspect;
        }
        else
        {
            // Window is too tall, shrink height
            renderSize.y = availableSize.x / targetAspect;
        }

        // Resize detection (using fixed aspect size)
        if (renderSize.x > 0 && renderSize.y > 0 && (std::abs(renderSize.x - m_ViewportSize.x) > 0.1f || std::abs(renderSize.y - m_ViewportSize.y) > 0.1f))
        {
            m_NextViewportSize = glm::vec2(renderSize.x, renderSize.y);
            m_ResizeTimer = 0.05f;
        }

        if (GetRenderPath())
        {
            auto& graph = GetRenderPath()->GetRenderGraph();
            std::string textureToDisplay = m_DebugViewTexture;

            // Handle switching and fallback
            static std::string lastTexture = "";
            if (textureToDisplay != lastTexture)
            {
                CH_CORE_INFO("EditorLayer: Viewport display switched to: '{}'", textureToDisplay.empty() ? "Final Output" : textureToDisplay);
                lastTexture = textureToDisplay;
            }

            if (textureToDisplay.empty() || !graph.ContainsImage(textureToDisplay))
            {
                // Fallback sequence: TAAOutput -> FinalColor -> RENDER_OUTPUT
                if (graph.ContainsImage("TAAOutput")) textureToDisplay = "TAAOutput";
                else if (graph.ContainsImage(RS::FinalColor)) textureToDisplay = RS::FinalColor;
                else textureToDisplay = RS::RENDER_OUTPUT;
            }

            if (graph.ContainsImage(textureToDisplay))
            {
                auto& img = graph.GetImage(textureToDisplay);
                if (img.handle != VK_NULL_HANDLE)
                {
                    // Use debug_view if available (handles depth linearization)
                    VkImageView viewToUse = img.debug_view != VK_NULL_HANDLE ? img.debug_view : img.view;
                    
                    ImTextureID textureID = Application::Get().GetImGuiLayer()->GetTextureID(viewToUse, ResourceManager::Get().GetDefaultSampler());
                    if (textureID)
                    {
                        // Center the image in the window
                        ImVec2 curPos = ImGui::GetCursorPos();
                        curPos.x += (availableSize.x - renderSize.x) * 0.5f;
                        curPos.y += (availableSize.y - renderSize.y) * 0.5f;
                        ImGui::SetCursorPos(curPos);

                        ImGui::Image(textureID, renderSize);
                    }
                }
            }
        }
    }
}
