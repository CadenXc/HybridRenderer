#pragma once

#include "Core/Layer.h"
#include "Renderer/Backend/VulkanContext.h"
#include <imgui.h>

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

		// �?ImGui 获取纹理句柄
		ImTextureID GetTextureID(VkImageView view, VkSampler sampler = VK_NULL_HANDLE);

		virtual void OnEvent(Event& e) override;

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
