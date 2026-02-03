#pragma once

#include "gfx/vulkan/Renderer.h"
#include "core/application/Application.h"

namespace Chimera {

	// A static helper class to expose Rendering functionality cleanly to Layers
	// similar to Walnut's Application::GetCommandBuffer() but more comprehensive
	class Render
	{
	public:
		static void WaitIdle()
		{
			vkDeviceWaitIdle(Application::Get().GetContext()->GetDevice());
		}

		static VkCommandBuffer BeginFrame()
		{
			// Note: Application::drawFrame() normally handles BeginFrame, but for custom rendering layers
			// they might need access to the current command buffer or context.
			// However, typically Layers render INSIDE the Application's render loop.
			// So we might expose the "Current" command buffer if one is active.
			// Renderer::BeginFrame creates a new one for the FRAME, but layers just record into it.
			return VK_NULL_HANDLE; 
		}

		static VkCommandBuffer GetCurrentCommandBuffer()
		{
			auto renderer = Application::Get().GetRenderer();
			if (renderer && renderer->IsFrameInProgress())
			{
				return renderer->GetActiveCommandBuffer();
			}
			return VK_NULL_HANDLE; 
		}
		
		// Expose Scene Loading via Render interface if desired?
		// Or strictly rendering commands.
	};
}