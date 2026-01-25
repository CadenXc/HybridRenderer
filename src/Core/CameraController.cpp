#include "pch.h"
#include "CameraController.h"

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
        if (m_Keys.Shift) velocity *= 2.0f;

        // Calculate Right vector
        glm::vec3 right = glm::normalize(glm::cross(m_Forward, m_Up));

        if (m_Keys.W) m_Position += m_Forward * velocity;
        if (m_Keys.S) m_Position -= m_Forward * velocity;
        if (m_Keys.A) m_Position -= right * velocity;
        if (m_Keys.D) m_Position += right * velocity;
        if (m_Keys.Q) m_Position -= m_Up * velocity;
        if (m_Keys.E) m_Position += m_Up * velocity;

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
        if (!m_RightMousePressed) {
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
        if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            if (action == GLFW_PRESS) {
                m_RightMousePressed = true;
            } else if (action == GLFW_RELEASE) {
                m_RightMousePressed = false;
                m_FirstMouse = true;
            }
        }
    }

    void CameraController::OnScroll(double xoffset, double yoffset)
    {
        float speed = 1.0f; // Scroll speed multiplier
        m_Position += m_Forward * (float)yoffset * speed;
        UpdateView();
    }

    void CameraController::OnKey(int key, int scancode, int action, int mods)
    {
        bool pressed = (action == GLFW_PRESS || action == GLFW_REPEAT);
        
        switch (key)
        {
            case GLFW_KEY_W: m_Keys.W = pressed; break;
            case GLFW_KEY_S: m_Keys.S = pressed; break;
            case GLFW_KEY_A: m_Keys.A = pressed; break;
            case GLFW_KEY_D: m_Keys.D = pressed; break;
            case GLFW_KEY_Q: m_Keys.Q = pressed; break;
            case GLFW_KEY_E: m_Keys.E = pressed; break;
            case GLFW_KEY_LEFT_SHIFT: m_Keys.Shift = pressed; break;
        }
    }
}
