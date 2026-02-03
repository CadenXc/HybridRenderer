#pragma once

#include "pch.h"
#include "core/scene/Scene.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

namespace Chimera {

	class CameraController
	{
	public:
		CameraController();

		void OnUpdate(float ts);
		void SetCamera(Camera* camera) { m_Camera = camera; }

		// Input callbacks (to be called from Application)
		void OnCursorPos(double xpos, double ypos);
		void OnMouseButton(int button, int action, int mods);
		void OnScroll(double xoffset, double yoffset);
		void OnKey(int key, int scancode, int action, int mods);

	private:
		void UpdateView();

	private:
		Camera* m_Camera = nullptr;

		// Camera State
		glm::vec3 m_Position{ 2.0f, 2.0f, 2.0f };
		glm::vec3 m_Forward{ 0.0f, 0.0f, -1.0f }; // Default OpenGL forward
		glm::vec3 m_Up{ 0.0f, 0.0f, 1.0f }; // Z-up for this project? (Vulkan is Y-down, but often adjusted)
		// Checking Scene initialization: lookAt(2,2,2, 0,0,0, 0,0,1) -> Z-up seems correct for world space.
		
		float m_Yaw = -135.0f; // Pointing towards origin from (2,2,2)
		float m_Pitch = -35.0f;

		float m_MovementSpeed = 2.5f;
		float m_MouseSensitivity = 0.1f;

		// Input State
		bool m_FirstMouse = true;
		float m_LastX = 800.0f / 2.0f;
		float m_LastY = 600.0f / 2.0f;
		bool m_RightMousePressed = false; // Only rotate when RMB is held

		// Keys
		struct KeyState
		{
			bool W = false;
			bool A = false;
			bool S = false;
			bool D = false;
			bool Q = false;
			bool E = false;
			bool Shift = false;
		} m_Keys;
	};
}