#pragma once

#include <imgui.h>
#include <glm/glm.hpp>

namespace Chimera {

	class ImGuiStyleManager
	{
	public:
		static void ApplyDarkTheme()
		{
			ImGuiStyle& style = ImGui::GetStyle();

			// Window Styling
			style.WindowRounding = 5.0f;
			style.WindowBorderSize = 0.0f;
			style.WindowPadding = ImVec2(10.0f, 10.0f);
			style.WindowMinSize = ImVec2(32.0f, 32.0f);

			// Frame Styling
			style.FrameRounding = 3.0f;
			style.FrameBorderSize = 0.0f;
			style.FramePadding = ImVec2(8.0f, 4.0f);

			// Item Spacing
			style.ItemSpacing = ImVec2(8.0f, 8.0f);
			style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
			style.IndentSpacing = 20.0f;

			// Button/Grab Styling
			style.GrabRounding = 3.0f;
			style.GrabMinSize = 10.0f;
			style.ButtonTextAlign = ImVec2(0.5f, 0.5f);

			// Color scheme - Professional dark theme
			ImVec4* colors = style.Colors;

			// Primary Backgrounds
			colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.10f, 0.94f);
			colors[ImGuiCol_ChildBg] = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
			colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.10f, 0.10f, 0.94f);

			// Frames
			colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.16f, 0.16f, 0.54f);
			colors[ImGuiCol_FrameBgHovered] = ImVec4(0.26f, 0.26f, 0.26f, 0.40f);
			colors[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.26f, 0.26f, 0.67f);

			// Title Bar
			colors[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
			colors[ImGuiCol_TitleBgActive] = ImVec4(0.29f, 0.29f, 0.29f, 1.00f);
			colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);

			// Menu Bar
			colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);

			// Scrollbar
			colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
			colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
			colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
			colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);

			// Buttons
			colors[ImGuiCol_Button] = ImVec4(0.25f, 0.25f, 0.25f, 0.40f);
			colors[ImGuiCol_ButtonHovered] = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
			colors[ImGuiCol_ButtonActive] = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);

			// Header/Collapsing
			colors[ImGuiCol_Header] = ImVec4(0.25f, 0.25f, 0.25f, 0.31f);
			colors[ImGuiCol_HeaderHovered] = ImVec4(0.35f, 0.35f, 0.35f, 0.80f);
			colors[ImGuiCol_HeaderActive] = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);

			// Separator
			colors[ImGuiCol_Separator] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
			colors[ImGuiCol_SeparatorHovered] = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
			colors[ImGuiCol_SeparatorActive] = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);

			// Resize Grip
			colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 0.20f);
			colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
			colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);

			// Tab
			colors[ImGuiCol_Tab] = ImVec4(0.18f, 0.35f, 0.58f, 0.86f);
			colors[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
			colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.41f, 0.68f, 1.00f);
			colors[ImGuiCol_TabUnfocused] = ImVec4(0.07f, 0.10f, 0.15f, 0.97f);
			colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.14f, 0.26f, 0.42f, 1.00f);

			// Docking
			colors[ImGuiCol_DockingPreview] = ImVec4(0.26f, 0.59f, 0.98f, 0.70f);
			colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);

			// Plot/Text
			colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
			colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
			colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
			colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);

			// Text & Checkmarks
			colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
			colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
			colors[ImGuiCol_CheckMark] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);

			// Slider/Input
			colors[ImGuiCol_SliderGrab] = ImVec4(0.26f, 0.59f, 0.98f, 0.78f);
			colors[ImGuiCol_SliderGrabActive] = ImVec4(0.46f, 0.54f, 0.80f, 0.60f);

			// Focused/Input (only include if supported by ImGui version)
			#ifdef ImGuiCol_InputTextBg
			colors[ImGuiCol_InputTextBg] = ImVec4(0.25f, 0.25f, 0.25f, 0.70f);
			colors[ImGuiCol_InputTextBgHovered] = ImVec4(0.35f, 0.35f, 0.35f, 0.80f);
			colors[ImGuiCol_InputTextBgActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.90f);
			colors[ImGuiCol_InputTextBgDisabled] = ImVec4(0.20f, 0.20f, 0.20f, 0.50f);
			#endif

			#ifdef ImGuiCol_FrameBgDisabled
			colors[ImGuiCol_FrameBgDisabled] = ImVec4(0.20f, 0.20f, 0.20f, 0.50f);
			#endif
			colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);

			// Navigation/Focus
			colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
			colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
			colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);

			// Modal Dimming
			colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
		}

		static void ApplyCompactStyle()
		{
			ImGuiStyle& style = ImGui::GetStyle();
			style.WindowPadding = ImVec2(6.0f, 6.0f);
			style.FramePadding = ImVec2(4.0f, 2.0f);
			style.ItemSpacing = ImVec2(4.0f, 4.0f);
			style.IndentSpacing = 12.0f;
			style.GrabMinSize = 8.0f;
		}

		static void ApplyDefaultStyle()
		{
			ImGui::StyleColorsDark();
		}
	};

}
