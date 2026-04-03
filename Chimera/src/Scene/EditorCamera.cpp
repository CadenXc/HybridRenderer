#include "pch.h"
#include "EditorCamera.h"
#include "Core/Input.h"
#include "Core/Application.h"

#include <glm/gtx/quaternion.hpp>

namespace Chimera
{
	EditorCamera::EditorCamera(float fov, float aspectRatio, float nearClip, float farClip)
		: m_FOV(fov), m_AspectRatio(aspectRatio), m_NearClip(nearClip), m_FarClip(farClip)
	{
		m_InitialMousePosition = { 0.0f, 0.0f };
		UpdateProjection();
		UpdateView();
	}

	void EditorCamera::UpdateProjection()
	{
		m_AspectRatio = m_ViewportWidth / m_ViewportHeight;
		
		float focalLength = 1.0f / tan(glm::radians(m_FOV) * 0.5f);
		float n = m_NearClip;
		float f = m_FarClip;

		m_Projection = glm::mat4(0.0f);
		m_Projection[0][0] = focalLength / m_AspectRatio;
		m_Projection[1][1] = -focalLength; // Vulkan Y is Down
		m_Projection[2][3] = -1.0f;

		if (USE_REVERSED_Z)
		{
			m_Projection[2][2] = n / (f - n);
			m_Projection[3][2] = (n * f) / (f - n);
		}
		else
		{
			m_Projection[2][2] = f / (n - f);
			m_Projection[3][2] = (n * f) / (n - f);
		}
        UpdateFrustum();
	}

	void EditorCamera::UpdateView()
	{
		m_Position = CalculatePosition();
		glm::quat orientation = GetOrientation();
		m_ViewMatrix = glm::translate(glm::mat4(1.0f), m_Position) * glm::toMat4(orientation);
		m_ViewMatrix = glm::inverse(m_ViewMatrix);
        UpdateFrustum();
	}

    void EditorCamera::UpdateFrustum()
    {
        m_Frustum = Frustum::FromViewProj(m_Projection * m_ViewMatrix);
    }

	void EditorCamera::OnUpdate(Timestep ts, bool isHovered, bool isFocused)
	{
		// [TAA] 保存历史状态
		m_TAA.PrevView = m_ViewMatrix;
		m_TAA.PrevProj = m_Projection;
		m_TAA.PrevJitter = m_TAA.CurrentJitter;

		const glm::vec2& mouse = { Input::GetMouseX(), Input::GetMouseY() };
		glm::vec2 delta = (mouse - m_InitialMousePosition) * 0.003f;
		m_InitialMousePosition = mouse;

		glm::vec3 oldFocalPoint = m_FocalPoint;
		float oldPitch = m_Pitch;
		float oldYaw = m_Yaw;
		float oldDistance = m_Distance;

		// 1. WASD Movement (UE Style)
		if (isFocused)
		{
			float moveSpeed = 5.0f * ts.GetSeconds(); 
			if (Input::IsKeyDown(KeyCode::LeftShift)) moveSpeed *= 2.5f;

			if (Input::IsKeyDown(KeyCode::W)) m_FocalPoint += GetForwardDirection() * moveSpeed;
			if (Input::IsKeyDown(KeyCode::S)) m_FocalPoint -= GetForwardDirection() * moveSpeed;
			if (Input::IsKeyDown(KeyCode::A)) m_FocalPoint -= GetRightDirection() * moveSpeed;
			if (Input::IsKeyDown(KeyCode::D)) m_FocalPoint += GetRightDirection() * moveSpeed;
			if (Input::IsKeyDown(KeyCode::E)) m_FocalPoint += glm::vec3(0.0f, 1.0f, 0.0f) * moveSpeed;
			if (Input::IsKeyDown(KeyCode::Q)) m_FocalPoint -= glm::vec3(0.0f, 1.0f, 0.0f) * moveSpeed;
		}

		// 2. Mouse Interaction (Alt Style)
		if ((Input::IsKeyDown(KeyCode::LeftAlt) || Input::IsKeyDown(KeyCode::RightAlt)) && isHovered)
		{
			if (Input::IsMouseButtonDown(MouseButton::Middle)) MousePan(delta);
			else if (Input::IsMouseButtonDown(MouseButton::Left)) MouseRotate(delta);
			else if (Input::IsMouseButtonDown(MouseButton::Right)) MouseZoom(delta.y);
		}

		if (m_FocalPoint != oldFocalPoint || m_Pitch != oldPitch || m_Yaw != oldYaw || m_Distance != oldDistance)
			m_IsUpdated = true;

		UpdateView();
	}

	static float GetHaltonSequence(int index, int base)
	{
		float f = 1.0f, r = 0.0f;
		int current = index;
		while (current > 0)
		{
			f = f / (float)base;
			r = r + f * (float)(current % base);
			current = current / base;
		}
		return r;
	}

	void EditorCamera::UpdateTAAState(uint32_t totalFrameCount, bool enabled)
	{
		if (!enabled || m_ViewportWidth == 0 || m_ViewportHeight == 0)
		{
			m_TAA.CurrentJitter = glm::vec2(0.0f);
			return;
		}

		int phase = (totalFrameCount % 16) + 1;
		float haltonX = GetHaltonSequence(phase, 2);
		float haltonY = GetHaltonSequence(phase, 3);

		m_TAA.CurrentJitter.x = (haltonX - 0.5f) * (2.0f / (float)m_ViewportWidth);
		m_TAA.CurrentJitter.y = (haltonY - 0.5f) * (2.0f / (float)m_ViewportHeight);
	}

	void EditorCamera::OnEvent(Event& e)
	{
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<MouseScrolledEvent>(BIND_EVENT_FN(EditorCamera::OnMouseScroll));
	}

	bool EditorCamera::OnMouseScroll(MouseScrolledEvent& e)
	{
		MouseZoom(e.GetYOffset() * 0.1f);
		m_IsUpdated = true;
		UpdateProjection(); 
		UpdateView();       
		return false;
	}

	void EditorCamera::Reset()
	{
		m_FocalPoint = { 0.0f, 0.0f, 0.0f };
		m_Distance = 10.0f;
		m_Pitch = m_Yaw = 0.0f;
		m_FOV = 45.0f;
		m_IsUpdated = true;
		UpdateProjection();
		UpdateView();
	}

	std::pair<float, float> EditorCamera::PanSpeed() const
	{
		float x = std::min(m_ViewportWidth / 1000.0f, 2.4f);
		float xFactor = 0.0366f * (x * x) - 0.1778f * x + 0.3021f;
		float y = std::min(m_ViewportHeight / 1000.0f, 2.4f);
		float yFactor = 0.0366f * (y * y) - 0.1778f * y + 0.3021f;
		return { xFactor, yFactor };
	}

	float EditorCamera::RotationSpeed() const { return 0.8f; }

	float EditorCamera::ZoomSpeed() const
	{
		float distance = std::max(m_Distance * 0.2f, 0.0f);
		return std::min(distance * distance, 100.0f);
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
		
		// 记录旋转前的相机绝对位置
		glm::vec3 currentPosition = CalculatePosition();

		// 更新角度
		m_Yaw += yawSign * delta.x * RotationSpeed();
		m_Pitch += delta.y * RotationSpeed();

		// [核心修改] 模拟“转头”动作：
		// 保持相机位置不变，将焦点推到视线前方固定距离处
		m_FocalPoint = currentPosition + GetForwardDirection() * m_Distance;
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

	glm::vec3 EditorCamera::GetUpDirection() const { return glm::rotate(GetOrientation(), glm::vec3(0.0f, 1.0f, 0.0f)); }
	glm::vec3 EditorCamera::GetRightDirection() const { return glm::rotate(GetOrientation(), glm::vec3(1.0f, 0.0f, 0.0f)); }
	glm::vec3 EditorCamera::GetForwardDirection() const { return glm::rotate(GetOrientation(), glm::vec3(0.0f, 0.0f, -1.0f)); }
	glm::vec3 EditorCamera::CalculatePosition() const { return m_FocalPoint - GetForwardDirection() * m_Distance; }
	glm::quat EditorCamera::GetOrientation() const { return glm::quat(glm::vec3(-m_Pitch, -m_Yaw, 0.0f)); }
}
