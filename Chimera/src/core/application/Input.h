#pragma once

#include "KeyCodes.h"
#include <glm/glm.hpp>

namespace Chimera {

	class Input
	{
	public:
		static bool IsKeyDown(KeyCode key);
		static bool IsMouseButtonDown(MouseButton button);

		static glm::vec2 GetMousePosition();
		static float GetMouseX();
		static float GetMouseY();

		static void SetCursorMode(CursorMode mode);
	};

}