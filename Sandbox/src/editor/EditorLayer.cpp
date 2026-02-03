#include "EditorLayer.h"
#include <imgui.h>
#include <filesystem>
#include <glm/gtc/type_ptr.hpp>

// Chimera Headers
#include "core/Config.h"
#include "core/application/Input.h"

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
		LoadDefaultModel();
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
		DrawStatsPanel();
		DrawRenderPathPanel();
		DrawModelSelectionPanel();
		DrawShaderSelectionPanel();
	}

	void EditorLayer::RefreshModelList()
	{
		m_AvailableModels.clear();
		if (std::filesystem::exists(m_CurrentLoadPath)) 
		{
			// Recursively find all model files and scene files in subdirectories
			for (const auto& entry : std::filesystem::recursive_directory_iterator(m_CurrentLoadPath)) 
			{
				std::string path_str = entry.path().string();
				
				// Check for direct model files (.obj, .gltf, .glb)
				if (entry.is_regular_file())
				{
					auto ext = entry.path().extension().string();
					if (ext == ".obj" || ext == ".gltf" || ext == ".glb") 
					{
						// Store relative path from m_CurrentLoadPath
						auto relative = std::filesystem::relative(entry.path(), m_CurrentLoadPath).string();
						// Replace backslashes with forward slashes for consistency
						std::replace(relative.begin(), relative.end(), '\\', '/');
						m_AvailableModels.push_back(relative);
					}
				}
				// Also check for scene.gltf/scene.glb in directories
				else if (entry.is_directory())
				{
					std::string scene_gltf = (entry.path() / "scene.gltf").string();
					std::string scene_glb = (entry.path() / "scene.glb").string();
					
					if (std::filesystem::exists(scene_gltf))
					{
						auto relative = std::filesystem::relative(std::filesystem::path(scene_gltf), m_CurrentLoadPath).string();
						std::replace(relative.begin(), relative.end(), '\\', '/');
						m_AvailableModels.push_back(relative);
					}
					else if (std::filesystem::exists(scene_glb))
					{
						auto relative = std::filesystem::relative(std::filesystem::path(scene_glb), m_CurrentLoadPath).string();
						std::replace(relative.begin(), relative.end(), '\\', '/');
						m_AvailableModels.push_back(relative);
					}
				}
			}
		}
		// Remove duplicates and sort for consistent ordering
		std::sort(m_AvailableModels.begin(), m_AvailableModels.end());
		m_AvailableModels.erase(std::unique(m_AvailableModels.begin(), m_AvailableModels.end()), m_AvailableModels.end());
	}

	void EditorLayer::LoadDefaultModel()
	{
		// Try to load pica_pica scene by default
		std::string defaultModel = "pica_pica_-_machines/scene.gltf";
		std::string fullPath = m_CurrentLoadPath + "/" + defaultModel;
		
		if (std::filesystem::exists(fullPath))
		{
			m_App->LoadScene(fullPath);
			m_ModelLoaded = true;
			// Find and set selected index
			auto dir_model = "pica_pica_-_machines";
			for (int i = 0; i < (int)m_AvailableModels.size(); i++)
			{
				if (m_AvailableModels[i].find(dir_model) != std::string::npos)
				{
					m_SelectedModelIndex = i;
					break;
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
				if (ImGui::MenuItem("Take Screenshot"))
				{
					std::filesystem::path screenshotDir = "D:/Graduation-project/HybridRenderer/Sandbox/screenshots";
					if (!std::filesystem::exists(screenshotDir))
						std::filesystem::create_directories(screenshotDir);
					
					auto now = std::chrono::system_clock::now();
					std::time_t now_time = std::chrono::system_clock::to_time_t(now);
					std::tm local_tm = *std::localtime(&now_time);
					
					std::ostringstream oss;
					oss << "Screenshot_"
						<< std::put_time(&local_tm, "%Y-%m-%d_%H-%M-%S")
						<< ".ppm";
					
					std::string filename = (screenshotDir / oss.str()).string();
					m_App->RequestScreenshot(filename);
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Exit", "Alt+F4")) m_App->Close();
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Render Path"))
			{
				if (ImGui::MenuItem("Forward", nullptr, m_App->GetCurrentRenderPathType() == RenderPathType::Forward))
					m_App->SwitchRenderPath(RenderPathType::Forward);
				if (ImGui::MenuItem("Ray Tracing", nullptr, m_App->GetCurrentRenderPathType() == RenderPathType::RayTracing))
					m_App->SwitchRenderPath(RenderPathType::RayTracing);
				if (ImGui::MenuItem("Hybrid", nullptr, m_App->GetCurrentRenderPathType() == RenderPathType::Hybrid))
					m_App->SwitchRenderPath(RenderPathType::Hybrid);
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Models")) {
				 for (int i = 0; i < (int)m_AvailableModels.size(); i++)
				{
					bool isSelected = (m_SelectedModelIndex == i);
					if (ImGui::MenuItem(m_AvailableModels[i].c_str(), nullptr, isSelected))
					{
						m_SelectedModelIndex = i;
						m_App->LoadScene(m_CurrentLoadPath + "/" + m_AvailableModels[i]);
					}
				}
				ImGui::EndMenu();
			}

			ImGui::EndMainMenuBar();
		}
	}

	void EditorLayer::DrawStatsPanel()
	{
		if (ImGui::Begin("Performance Statistics"))
		{
			float fps = (m_AverageFrameTime > 0.0f) ? (1000.0f / m_AverageFrameTime) : 0.0f;
			ImGui::Text("Frame Time: %.3f ms", m_AverageFrameTime);
			ImGui::TextColored(fps >= 60 ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "FPS: %.1f", fps);
			
			ImGui::Separator();
			ImGui::PlotLines("##FrameTimeHistory", m_FrameTimeHistory.data(), (int)m_FrameTimeHistory.size(), 0, "Frame Time", 0.0f, 33.0f, ImVec2(0, 80));
			ImGui::End();
		}
	}

	void EditorLayer::DrawRenderPathPanel()
	{
		if (ImGui::Begin("Render Path Configuration"))
		{
			// Delegate UI drawing to the active render path
			if (auto renderPath = m_App->GetRenderPath()) {
				renderPath->OnImGui();
			} else {
				ImGui::Text("No active render path.");
			}
			ImGui::End();
		}
	}

	void EditorLayer::DrawModelSelectionPanel()
	{
		if (ImGui::Begin("Model Selection", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Available Models:");
			ImGui::Separator();
			
			// Combo box for model selection
			std::vector<const char*> model_names;
			for (const auto& model : m_AvailableModels) {
				model_names.push_back(model.c_str());
			}
			
			if (!model_names.empty())
			{
				if (ImGui::Combo("##ModelList", &m_SelectedModelIndex, 
								model_names.data(), (int)model_names.size()))
				{
					if (m_SelectedModelIndex >= 0 && m_SelectedModelIndex < (int)m_AvailableModels.size())
					{
						std::string fullPath = m_CurrentLoadPath + "/" + m_AvailableModels[m_SelectedModelIndex];
						m_App->LoadScene(fullPath);
						m_ModelLoaded = true;
					}
				}
			}
			else
			{
				ImGui::Text("No models found in %s", m_CurrentLoadPath.c_str());
			}
			
			ImGui::Separator();
			if (ImGui::Button("Refresh Models", ImVec2(120, 0)))
			{
				RefreshModelList();
			}
			
			ImGui::SameLine();
			if (ImGui::Button("Reload Current", ImVec2(120, 0)))
			{
				if (m_SelectedModelIndex >= 0 && m_SelectedModelIndex < (int)m_AvailableModels.size())
				{
					std::string fullPath = m_CurrentLoadPath + "/" + m_AvailableModels[m_SelectedModelIndex];
					m_App->LoadScene(fullPath);
				}
			}
			
			ImGui::End();
		}
	}

	void EditorLayer::DrawShaderSelectionPanel()
	{
		if (ImGui::Begin("Render Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Render Path:");
			ImGui::Separator();
			
			if (ImGui::Combo("##RenderPath", &m_SelectedRenderPathIndex, 
							m_RenderPathNames, 3))
			{
				if (m_SelectedRenderPathIndex == 0)
					m_App->SwitchRenderPath(RenderPathType::Forward);
				else if (m_SelectedRenderPathIndex == 1)
					m_App->SwitchRenderPath(RenderPathType::RayTracing);
				else if (m_SelectedRenderPathIndex == 2)
					m_App->SwitchRenderPath(RenderPathType::Hybrid);
			}
			
			ImGui::Separator();
			ImGui::Text("Current: %s", m_RenderPathNames[m_SelectedRenderPathIndex]);
			
			ImGui::End();
		}
	}
}
