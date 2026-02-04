#include "pch.h"
#include "Scene/CameraController.h"

#include "Core/Input.h"
#include "Core/KeyCodes.h"

namespace Chimera {

	CameraController::CameraController()
	{
		// Calculate initial forward vector based on default yaw/pitch
		glm::vec3 front;
		front.x = cos(glm::radians(m_Yaw)) * cos(glm::radians(m_Pitch));
		front.y = sin(glm::radians(m_Yaw)) * cos(glm::radians(m_Pitch));
		front.z = sin(glm::radians(m_Pitch));
		m_Forward = glm::normalize(front);
	}

	void CameraController::OnUpdate(float ts)
	{
		if (!m_Camera) return;

		float velocity = m_MovementSpeed * ts;
		if (Input::IsKeyDown(KeyCode::LeftShift)) velocity *= 5.0f;

		// Calculate Right vector
		glm::vec3 right = glm::normalize(glm::cross(m_Forward, m_Up));

		if (Input::IsKeyDown(KeyCode::W)) m_Position += m_Forward * velocity;
		if (Input::IsKeyDown(KeyCode::S)) m_Position -= m_Forward * velocity;
		if (Input::IsKeyDown(KeyCode::A)) m_Position -= right * velocity;
		if (Input::IsKeyDown(KeyCode::D)) m_Position += right * velocity;
		if (Input::IsKeyDown(KeyCode::Q)) m_Position -= m_Up * velocity;
		if (Input::IsKeyDown(KeyCode::E)) m_Position += m_Up * velocity;

		UpdateView();
	}

	void CameraController::UpdateView()
	{
		if (!m_Camera) return;
		m_Camera->view = glm::lookAt(m_Position, m_Position + m_Forward, m_Up);
		m_Camera->viewInverse = glm::inverse(m_Camera->view);
	}

	void CameraController::OnCursorPos(double xpos, double ypos)
	{
		if (!Input::IsMouseButtonDown(MouseButton::Right)) {
			m_FirstMouse = true;
			return;
		}

		if (m_FirstMouse)
		{
			m_LastX = static_cast<float>(xpos);
			m_LastY = static_cast<float>(ypos);
			m_FirstMouse = false;
		}

		float xoffset = static_cast<float>(xpos) - m_LastX;
		float yoffset = m_LastY - static_cast<float>(ypos); // Reversed since y-coordinates go from bottom to top

		m_LastX = static_cast<float>(xpos);
		m_LastY = static_cast<float>(ypos);

		xoffset *= m_MouseSensitivity;
		yoffset *= m_MouseSensitivity;

		m_Yaw   += xoffset;
		m_Pitch += yoffset;

		// Constrain pitch
		if (m_Pitch > 89.0f) m_Pitch = 89.0f;
		if (m_Pitch < -89.0f) m_Pitch = -89.0f;

		glm::vec3 front;
		front.x = cos(glm::radians(m_Yaw)) * cos(glm::radians(m_Pitch));
		front.y = sin(glm::radians(m_Yaw)) * cos(glm::radians(m_Pitch));
		front.z = sin(glm::radians(m_Pitch));
		m_Forward = glm::normalize(front);
	}

	void CameraController::OnMouseButton(int button, int action, int mods)
	{
	}

	void CameraController::OnScroll(double xoffset, double yoffset)
	{
		float speed = 1.0f; // Scroll speed multiplier
		m_Position += m_Forward * (float)yoffset * speed;
		UpdateView();
	}

	void CameraController::OnKey(int key, int scancode, int action, int mods)
	{
	}
}

