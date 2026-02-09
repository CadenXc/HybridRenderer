#include "pch.h"
#include "Input.h"
#include "Core/Window.h"
#include "Core/Application.h"
#include <GLFW/glfw3.h>

namespace Chimera
{
    bool Input::IsKeyDown(KeyCode key)
    {
        auto window = Application::Get().GetWindow().GetNativeWindow();
        if (!window)
        {
            return false;
        }
        auto state = glfwGetKey(window, static_cast<int32_t>(key));
        return state == GLFW_PRESS || state == GLFW_REPEAT;
    }

    bool Input::IsMouseButtonDown(MouseButton button)
    {
        auto window = Application::Get().GetWindow().GetNativeWindow();
        if (!window)
        {
            return false;
        }
        auto state = glfwGetMouseButton(window, static_cast<int32_t>(button));
        return state == GLFW_PRESS;
    }

    glm::vec2 Input::GetMousePosition()
    {
        auto window = Application::Get().GetWindow().GetNativeWindow();
        if (!window)
        {
            return { 0.0f, 0.0f };
        }
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);
        return { (float)xpos, (float)ypos };
    }

    float Input::GetMouseX()
    {
        return GetMousePosition().x;
    }

    float Input::GetMouseY()
    {
        return GetMousePosition().y;
    }

    void Input::SetCursorMode(CursorMode mode)
    {
        auto window = Application::Get().GetWindow().GetNativeWindow();
        if (!window)
        {
            return;
        }
        int inputMode = GLFW_CURSOR_NORMAL;
        switch (mode)
        {
            case CursorMode::Normal:
                inputMode = GLFW_CURSOR_NORMAL;
                break;
            case CursorMode::Hidden:
                inputMode = GLFW_CURSOR_HIDDEN;
                break;
            case CursorMode::Locked:
                inputMode = GLFW_CURSOR_DISABLED;
                break;
        }
        glfwSetInputMode(window, GLFW_CURSOR, inputMode);
    }
}
