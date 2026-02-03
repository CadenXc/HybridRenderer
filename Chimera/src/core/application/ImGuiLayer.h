#pragma once

#include "core/application/Layer.h"
#include "gfx/vulkan/VulkanContext.h"

namespace Chimera {

	class ImGuiLayer : public Layer
	{
	public:
		ImGuiLayer(std::shared_ptr<VulkanContext> context);
		~ImGuiLayer();

		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnUIRender() override; // ImGuiLayer itself might draw some debug UI

		void Begin();
		void End(VkCommandBuffer cmd, VkImageView targetView, VkExtent2D extent);

		void BlockEvents(bool block) { m_BlockEvents = block; }
		bool BlockEvents() const { return m_BlockEvents; }
		
		// Configuration
		void SetDarkThemeColors();

	private:
		std::shared_ptr<VulkanContext> m_Context;
		VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
		bool m_BlockEvents = false;
	};

}
