#include "pch.h"
#include "Window.h"
#include "Core/Log.h"
#include "Core/Events/ApplicationEvent.h"
#include "Core/Events/KeyEvent.h"
#include "Core/Events/MouseEvent.h"

#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>

namespace Chimera {

	static bool s_GLFWInitialized = false;

	class WindowsWindow : public Window
	{
	public:
		WindowsWindow(const WindowProps& props)
		{
			Init(props);
		}

		virtual ~WindowsWindow()
		{
			Shutdown();
		}

		void OnUpdate() override
		{
			glfwPollEvents();
		}

		uint32_t GetWidth() const override { return m_Data.Width; }
		uint32_t GetHeight() const override { return m_Data.Height; }

		void SetEventCallback(const EventCallbackFn& callback) override
		{
			m_Data.EventCallback = callback;
		}

		GLFWwindow* GetNativeWindow() const override { return m_Window; }

	private:
		virtual void Init(const WindowProps& props)
		{
			m_Data.Title = props.Title;
			m_Data.Width = props.Width;
			m_Data.Height = props.Height;

			CH_CORE_INFO("Creating window {0} ({1}, {2})", props.Title, props.Width, props.Height);

			if (!s_GLFWInitialized)
			{
				int success = glfwInit();
				if (!success)
				{
					CH_CORE_ERROR("Could not initialize GLFW!");
					return;
				}
				s_GLFWInitialized = true;
			}

			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
			glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

			m_Window = glfwCreateWindow((int)props.Width, (int)props.Height, m_Data.Title.c_str(), nullptr, nullptr);

			glfwSetWindowUserPointer(m_Window, &m_Data);

			// --- GLFW Callbacks -> Chimera Events & ImGui ---

			glfwSetFramebufferSizeCallback(m_Window, [](GLFWwindow* window, int width, int height)
			{
				WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
				data.Width = width;
				data.Height = height;
				
				WindowResizeEvent event(width, height);
				if (data.EventCallback) data.EventCallback(event);
			});

			glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window)
			{
				WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
				WindowCloseEvent event;
				if (data.EventCallback) data.EventCallback(event);
			});

			glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int scancode, int action, int mods)
			{
				ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
				WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
				if (!data.EventCallback) return;

				switch (action)
				{
					case GLFW_PRESS:
					{
						KeyPressedEvent event(static_cast<KeyCode>(key), 0);
						data.EventCallback(event);
						break;
					}
					case GLFW_RELEASE:
					{
						KeyReleasedEvent event(static_cast<KeyCode>(key));
						data.EventCallback(event);
						break;
					}
					case GLFW_REPEAT:
					{
						KeyPressedEvent event(static_cast<KeyCode>(key), 1);
						data.EventCallback(event);
						break;
					}
				}
			});

			glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int mods)
			{
				ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
				WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
				if (!data.EventCallback) return;

				switch (action)
				{
					case GLFW_PRESS:
					{
						MouseButtonPressedEvent event(static_cast<MouseButton>(button));
						data.EventCallback(event);
						break;
					}
					case GLFW_RELEASE:
					{
						MouseButtonReleasedEvent event(static_cast<MouseButton>(button));
						data.EventCallback(event);
						break;
					}
				}
			});

			glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double xOffset, double yOffset)
			{
				ImGui_ImplGlfw_ScrollCallback(window, xOffset, yOffset);
				WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
				MouseScrolledEvent event((float)xOffset, (float)yOffset);
				if (data.EventCallback) data.EventCallback(event);
			});

			glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double xPos, double yPos)
			{
				ImGui_ImplGlfw_CursorPosCallback(window, xPos, yPos);
				WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
				MouseMovedEvent event((float)xPos, (float)yPos);
				if (data.EventCallback) data.EventCallback(event);
			});

			glfwSetCharCallback(m_Window, [](GLFWwindow* window, unsigned int c)
			{
				ImGui_ImplGlfw_CharCallback(window, c);
			});

			glfwSetCursorEnterCallback(m_Window, [](GLFWwindow* window, int entered)
			{
				ImGui_ImplGlfw_CursorEnterCallback(window, entered);
			});

			glfwSetWindowFocusCallback(m_Window, [](GLFWwindow* window, int focused)
			{
				ImGui_ImplGlfw_WindowFocusCallback(window, focused);
			});
		}

		virtual void Shutdown()
		{
			glfwDestroyWindow(m_Window);
		}

	private:
		GLFWwindow* m_Window;

		struct WindowData
		{
			std::string Title;
			uint32_t Width, Height;
			EventCallbackFn EventCallback;
		};

		WindowData m_Data;
	};

	std::unique_ptr<Window> Window::Create(const WindowProps& props)
	{
		return std::make_unique<WindowsWindow>(props);
	}

}