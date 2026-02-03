#pragma once

#include "pch.h"
#include "gfx/vulkan/VulkanContext.h"
#include "core/scene/Scene.h"
#include "gfx/resources/ResourceManager.h"

namespace Chimera {

	enum class RenderPathType
	{
		Forward,
		RayTracing,
		Hybrid
	};

	class RenderPath
	{
	public:
		RenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager)
			: m_Context(context), m_Scene(scene), m_ResourceManager(resourceManager) {}
		
		virtual ~RenderPath() = default;

		virtual void Init() = 0;
		virtual void OnSceneUpdated() {} 
		virtual void OnImGui() {}
		virtual void Render(VkCommandBuffer cmd, uint32_t currentFrame, uint32_t imageIndex, 
							VkDescriptorSet globalDescriptorSet, const std::vector<VkImage>& swapChainImages,
							std::function<void(VkCommandBuffer)> uiDrawCallback = nullptr) = 0;

	protected:
		// 检查并处理尺寸调整（子类在 Render 开头调用）
		void EnsureResources(uint32_t width, uint32_t height)
		{
			if (m_LastExtent.width != width || m_LastExtent.height != height)
			{
				vkDeviceWaitIdle(m_Context->GetDevice());
				OnRecreateResources(width, height);
				m_LastExtent = { width, height };
			}
		}

		// 具体的资源重建逻辑由子类实现
		virtual void OnRecreateResources(uint32_t width, uint32_t height) = 0;

	protected:
		std::shared_ptr<VulkanContext> m_Context;
		std::shared_ptr<Scene> m_Scene;
		ResourceManager* m_ResourceManager;
		VkExtent2D m_LastExtent = { 0, 0 };
	};

}
