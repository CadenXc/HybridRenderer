#pragma once

#include "pch.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Scene/Scene.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/Graph/RenderGraph.h"

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
		RenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager, PipelineManager& pipelineManager)
			: m_Context(context), m_Scene(scene), m_ResourceManager(resourceManager), m_PipelineManager(pipelineManager) {}
		
		virtual ~RenderPath() = default;

		virtual void Init()
		{
			m_LastExtent = m_Context->GetSwapChainExtent();
			RebuildGraph();
		}

		virtual void SetupGraph(RenderGraph& graph) = 0;
		virtual RenderPathType GetType() const = 0;
		
		virtual void OnSceneUpdated() 
		{
			m_NeedsRebuild = true;
		}

		virtual void SetScene(std::shared_ptr<Scene> scene)
		{
			m_Scene = scene;
			OnSceneUpdated();
		}

		RenderGraph& GetRenderGraph() { return *m_RenderGraph; }

		virtual void OnImGui() {}

		virtual void Render(VkCommandBuffer cmd, uint32_t currentFrame, uint32_t imageIndex, 
							VkDescriptorSet globalDescriptorSet, const std::vector<VkImage>& swapChainImages,
							std::function<void(VkCommandBuffer)> uiDrawCallback = nullptr)
		{
			// 1. 检查窗口缩�?
			auto extent = m_Context->GetSwapChainExtent();
			if (m_LastExtent.width != extent.width || m_LastExtent.height != extent.height)
			{
				vkDeviceWaitIdle(m_Context->GetDevice());
				m_NeedsRebuild = true;
				m_LastExtent = extent;
			}

			// 2. 检查是否需要重建（缩放或场景变更）
			if (m_NeedsRebuild)
			{
				vkDeviceWaitIdle(m_Context->GetDevice());
				RebuildGraph();
				m_NeedsRebuild = false;
			}

			// 3. 执行渲染�?
			if (m_RenderGraph)
			{
				m_RenderGraph->Execute(cmd, currentFrame, imageIndex, uiDrawCallback);
			}
		}

	protected:
		void RebuildGraph()
		{
			m_RenderGraph = std::make_unique<RenderGraph>(*m_Context, *m_ResourceManager, m_PipelineManager);
			SetupGraph(*m_RenderGraph);
		}

		std::shared_ptr<VulkanContext> m_Context;
		std::shared_ptr<Scene> m_Scene;
		ResourceManager* m_ResourceManager;
		PipelineManager& m_PipelineManager;
		
		std::unique_ptr<RenderGraph> m_RenderGraph;
		VkExtent2D m_LastExtent = { 0, 0 };
		bool m_NeedsRebuild = false;
	};

}
