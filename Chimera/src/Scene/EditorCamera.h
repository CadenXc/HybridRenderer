#pragma once

#include "Core/Events/Event.h"
#include "Core/Events/MouseEvent.h"
#include "Core/Timestep.h"
#include <glm/glm.hpp>

namespace Chimera
{

	class EditorCamera
	{
	public:
		EditorCamera() = default;
		EditorCamera(float fov, float aspectRatio, float nearClip, float farClip);

		void OnUpdate(Timestep ts, bool isHovered, bool isFocused);
		void OnEvent(Event& e);

		void Reset();

		bool IsUpdated() const
		{
			return m_IsUpdated;
		}
		void ClearUpdateFlag()
		{
			m_IsUpdated = false;
		}

		inline float GetDistance() const
		{
			return m_Distance;
		}
		inline void SetDistance(float distance)
		{
			m_Distance = distance;
		}

		inline float GetNearClip() const
		{
			return m_NearClip;
		}
		inline void SetNearClip(float nearClip)
		{
			m_NearClip = nearClip; UpdateProjection();
		}
		inline float GetFarClip() const
		{
			return m_FarClip;
		}
		inline void SetFarClip(float farClip)
		{
			m_FarClip = farClip; UpdateProjection();
		}

		inline glm::vec3 GetFocalPoint() const
		{
			return m_FocalPoint;
		}
		inline void SetFocalPoint(const glm::vec3& focalPoint)
		{
			m_FocalPoint = focalPoint;
		}

		inline void SetViewportSize(float width, float height)
		{
			m_ViewportWidth = width; m_ViewportHeight = height; UpdateProjection();
		}

		const glm::mat4& GetViewMatrix() const
		{
			return m_ViewMatrix;
		}
		glm::mat4 GetViewProjection() const
		{
			return m_Projection * m_ViewMatrix;
		}
		const glm::mat4& GetProjection() const
		{
			return m_Projection;
		}

		glm::vec3 GetUpDirection() const;
		glm::vec3 GetRightDirection() const;
		glm::vec3 GetForwardDirection() const;
		const glm::vec3& GetPosition() const
		{
			return m_Position;
		}
		glm::quat GetOrientation() const;

		float GetPitch() const
		{
			return m_Pitch;
		}
		float GetYaw() const
		{
			return m_Yaw;
		}
		float GetFOV() const
		{
			return m_FOV;
		}
		void SetFOV(float fov)
		{
			m_FOV = fov; UpdateProjection();
		}

	private:
		void UpdateProjection();
		void UpdateView();

		bool OnMouseScroll(MouseScrolledEvent& e);

		void MousePan(const glm::vec2& delta);
		void MouseRotate(const glm::vec2& delta);
		void MouseZoom(float delta);
		void MouseFOV(float delta);

		glm::vec3 CalculatePosition() const;

		std::pair<float, float> PanSpeed() const;
		float RotationSpeed() const;
		float ZoomSpeed() const;

	private:
		float m_FOV = 45.0f;
		float m_AspectRatio = 1.778f;
		float m_NearClip = 0.1f;
		float m_FarClip = 1000.0f;

		glm::mat4 m_ViewMatrix;
		glm::mat4 m_Projection = glm::mat4(1.0f);
		glm::vec3 m_Position = { 0.0f, 0.0f, 0.0f };
		glm::vec3 m_FocalPoint = { 0.0f, 0.0f, 0.0f };

		glm::vec2 m_InitialMousePosition = { 0.0f, 0.0f };

		float m_Distance = 5.0f;
		float m_Pitch = 0.0f;
		float m_Yaw = 0.0f;

		float m_ViewportWidth = 1600.0f;
		float m_ViewportHeight = 900.0f;

		bool m_IsUpdated = false;
	};

}
