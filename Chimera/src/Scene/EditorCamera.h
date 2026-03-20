#pragma once

#include "Renderer/Camera.h"
#include "Core/Events/Event.h"
#include "Core/Events/MouseEvent.h"
#include "Core/Timestep.h"
#include <glm/glm.hpp>

namespace Chimera
{
	class EditorCamera : public Camera
	{
	public:
		EditorCamera() = default;
		EditorCamera(float fov, float aspectRatio, float nearClip, float farClip);

		void OnUpdate(Timestep ts, bool isHovered, bool isFocused);
		void OnEvent(Event& e);

		// [TAA] 管理接口
		void UpdateTAAState(uint32_t totalFrameCount, bool enabled);
		const glm::vec2& GetJitter() const { return m_TAA.CurrentJitter; }
		const glm::vec2& GetPrevJitter() const { return m_TAA.PrevJitter; }
		const glm::mat4& GetPrevView() const { return m_TAA.PrevView; }
		const glm::mat4& GetPrevProj() const { return m_TAA.PrevProj; }

		// [State] 查询与操作
		bool IsUpdated() const { return m_IsUpdated; }
		void ClearUpdateFlag() { m_IsUpdated = false; }
		void Reset();

		// [Getters/Setters]
		inline float GetDistance() const { return m_Distance; }
		inline void SetDistance(float distance) { m_Distance = distance; }
		
		inline glm::vec3 GetFocalPoint() const { return m_FocalPoint; }
		inline void SetFocalPoint(const glm::vec3& focalPoint) { m_FocalPoint = focalPoint; UpdateView(); }

		inline float GetNearClip() const { return m_NearClip; }
		inline void SetNearClip(float nearClip) { m_NearClip = nearClip; UpdateProjection(); }
		inline float GetFarClip() const { return m_FarClip; }
		inline void SetFarClip(float farClip) { m_FarClip = farClip; UpdateProjection(); }

		inline void SetViewportSize(float width, float height) { m_ViewportWidth = width; m_ViewportHeight = height; UpdateProjection(); }
		
		const glm::mat4& GetViewMatrix() const { return m_ViewMatrix; }
		glm::mat4 GetViewProjection() const { return m_Projection * m_ViewMatrix; }

		glm::vec3 GetUpDirection() const;
		glm::vec3 GetRightDirection() const;
		glm::vec3 GetForwardDirection() const;
		const glm::vec3& GetPosition() const { return m_Position; }
		glm::quat GetOrientation() const;

		float GetPitch() const { return m_Pitch; }
		float GetYaw() const { return m_Yaw; }
		float GetFOV() const { return m_FOV; }
		void SetFOV(float fov) { m_FOV = fov; UpdateProjection(); }

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
		// 相机核心属性 (参考 Hazel 命名风格)
		float m_FOV = 45.0f, m_AspectRatio = 1.778f, m_NearClip = 0.1f, m_FarClip = 1000.0f;
		float m_ViewportWidth = 1600.0f, m_ViewportHeight = 900.0f;

		glm::mat4 m_ViewMatrix;
		glm::vec3 m_Position = { 0.0f, 0.0f, 0.0f };
		glm::vec3 m_FocalPoint = { 0.0f, 0.0f, 0.0f };

		// 交互控制
		glm::vec2 m_InitialMousePosition = { 0.0f, 0.0f };
		float m_Distance = 10.0f;
		float m_Pitch = 0.0f, m_Yaw = 0.0f;

		bool m_IsUpdated = false;

		// [TAA] 专门封装 TAA 相关的历史数据，不再散落在主类成员中
		struct TAAData
		{
			glm::vec2 CurrentJitter = { 0.0f, 0.0f };
			glm::vec2 PrevJitter = { 0.0f, 0.0f };
			glm::mat4 PrevView = glm::mat4(1.0f);
			glm::mat4 PrevProj = glm::mat4(1.0f);
		} m_TAA;
	};
}
