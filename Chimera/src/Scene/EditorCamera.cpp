#include "pch.h"
#include "EditorCamera.h"
#include "Core/Input.h"
#include "Core/Application.h"

#include <glm/gtx/quaternion.hpp>
#include <imgui.h>

namespace Chimera {

	EditorCamera::EditorCamera(float fov, float aspectRatio, float nearClip, float farClip)
		: m_FOV(fov), m_AspectRatio(aspectRatio), m_NearClip(nearClip), m_FarClip(farClip), m_ViewMatrix(glm::mat4(1.0f))
	{
		m_InitialMousePosition = { 0.0f, 0.0f };
		UpdateProjection();
		UpdateView();
	}

	void EditorCamera::UpdateProjection()
	{
		m_AspectRatio = m_ViewportWidth / m_ViewportHeight;
		m_Projection = glm::perspective(glm::radians(m_FOV), m_AspectRatio, m_NearClip, m_FarClip);
		m_Projection[1][1] *= -1;
	}

	void EditorCamera::UpdateView()
	{
		// M = T * R
		// Position = FocalPoint - Forward * Distance
		m_Position = CalculatePosition();

		glm::quat orientation = GetOrientation();
		m_ViewMatrix = glm::translate(glm::mat4(1.0f), m_Position) * glm::toMat4(orientation);
		m_ViewMatrix = glm::inverse(m_ViewMatrix);
	}

	std::pair<float, float> EditorCamera::PanSpeed() const
	{
		float x = std::min(m_ViewportWidth / 1000.0f, 2.4f); // max = 2.4f
		float xFactor = 0.0366f * (x * x) - 0.1778f * x + 0.3021f;

		float y = std::min(m_ViewportHeight / 1000.0f, 2.4f); // max = 2.4f
		float yFactor = 0.0366f * (y * y) - 0.1778f * y + 0.3021f;

		return { xFactor, yFactor };
	}

	float EditorCamera::RotationSpeed() const
	{
		return 0.8f;
	}

	float EditorCamera::ZoomSpeed() const
	{
		float distance = m_Distance * 0.2f;
		distance = std::max(distance, 0.0f);
		float speed = distance * distance;
		speed = std::min(speed, 100.0f); // max speed = 100
		return speed;
	}

	void EditorCamera::OnUpdate(Timestep ts, bool isHovered, bool isFocused)
	{
		const glm::vec2& mouse = { Input::GetMouseX(), Input::GetMouseY() };
		glm::vec2 delta = (mouse - m_InitialMousePosition) * 0.003f;
		m_InitialMousePosition = mouse;

		glm::vec3 oldFocalPoint = m_FocalPoint;
		float oldPitch = m_Pitch;
		float oldYaw = m_Yaw;
		float oldDistance = m_Distance;

		// 1. Keyboard Movement (UE Style) - Only if focused
		if (isFocused)
		{
			float moveSpeed = 5.0f * ts.GetSeconds(); 
			if (Input::IsKeyDown(KeyCode::LeftShift)) moveSpeed *= 2.5f; // Sprint

			if (Input::IsKeyDown(KeyCode::W))
				m_FocalPoint += GetForwardDirection() * moveSpeed;
			if (Input::IsKeyDown(KeyCode::S))
				m_FocalPoint -= GetForwardDirection() * moveSpeed;
			if (Input::IsKeyDown(KeyCode::A))
				m_FocalPoint -= GetRightDirection() * moveSpeed;
			if (Input::IsKeyDown(KeyCode::D))
				m_FocalPoint += GetRightDirection() * moveSpeed;
			if (Input::IsKeyDown(KeyCode::E))
				m_FocalPoint += glm::vec3(0.0f, 1.0f, 0.0f) * moveSpeed;
			if (Input::IsKeyDown(KeyCode::Q))
				m_FocalPoint -= glm::vec3(0.0f, 1.0f, 0.0f) * moveSpeed;
		}

		// 2. Mouse Interaction (Alt + Click) - Only if hovered
		if ((Input::IsKeyDown(KeyCode::LeftAlt) || Input::IsKeyDown(KeyCode::RightAlt)) && isHovered)
		{
			if (Input::IsMouseButtonDown(MouseButton::Middle))
				MousePan(delta);
			else if (Input::IsMouseButtonDown(MouseButton::Left))
				MouseRotate(delta);
			else if (Input::IsMouseButtonDown(MouseButton::Right))
				MouseZoom(delta.y);
		}

		if (m_FocalPoint != oldFocalPoint || m_Pitch != oldPitch || m_Yaw != oldYaw || m_Distance != oldDistance)
			m_IsUpdated = true;

		UpdateView();
	}

	void EditorCamera::OnEvent(Event& e)
	{
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<MouseScrolledEvent>(BIND_EVENT_FN(EditorCamera::OnMouseScroll));
	}

	bool EditorCamera::OnMouseScroll(MouseScrolledEvent& e)
	{
		float delta = e.GetYOffset();
		MouseZoom(delta * 0.1f);

		m_IsUpdated = true;
		UpdateProjection(); 
		UpdateView();       
		return false;
	}

	void EditorCamera::MouseFOV(float delta)
	{
		m_FOV -= delta * 2.0f;
		if (m_FOV < 1.0f) m_FOV = 1.0f;
		if (m_FOV > 120.0f) m_FOV = 120.0f;
	}

	void EditorCamera::Reset()
	{
		m_FocalPoint = { 0.0f, 0.0f, 0.0f };
		m_Distance = 10.0f;
		m_Pitch = 0.0f;
		m_Yaw = 0.0f;
		m_FOV = 45.0f;
		m_IsUpdated = true;
		UpdateProjection();
		UpdateView();
	}

	void EditorCamera::MousePan(const glm::vec2& delta)
	{
		auto [xSpeed, ySpeed] = PanSpeed();
		m_FocalPoint += -GetRightDirection() * delta.x * xSpeed * m_Distance;
		m_FocalPoint += GetUpDirection() * delta.y * ySpeed * m_Distance;
	}

	void EditorCamera::MouseRotate(const glm::vec2& delta)
	{
		float yawSign = GetUpDirection().y < 0 ? -1.0f : 1.0f;
		m_Yaw += yawSign * delta.x * RotationSpeed();
		m_Pitch += delta.y * RotationSpeed(); 
	}

	void EditorCamera::MouseZoom(float delta)
	{
		m_Distance -= delta * ZoomSpeed();
		if (m_Distance < 1.0f)
		{
			m_FocalPoint += GetForwardDirection();
			m_Distance = 1.0f;
		}
	}

	glm::vec3 EditorCamera::GetUpDirection() const
	{
		return glm::rotate(GetOrientation(), glm::vec3(0.0f, 1.0f, 0.0f));
	}

	glm::vec3 EditorCamera::GetRightDirection() const
	{
		return glm::rotate(GetOrientation(), glm::vec3(1.0f, 0.0f, 0.0f));
	}

	glm::vec3 EditorCamera::GetForwardDirection() const
	{
		return glm::rotate(GetOrientation(), glm::vec3(0.0f, 0.0f, -1.0f));
	}

	glm::vec3 EditorCamera::CalculatePosition() const
	{
		return m_FocalPoint - GetForwardDirection() * m_Distance;
	}

	glm::quat EditorCamera::GetOrientation() const
	{
		return glm::quat(glm::vec3(-m_Pitch, -m_Yaw, 0.0f));
	}

}
