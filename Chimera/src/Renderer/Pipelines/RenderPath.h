#pragma once

#include "pch.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Scene/Scene.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/Graph/RenderGraph.h"

namespace Chimera {

    // Forward declaration to break circular dependency
    class Application;

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
            if (m_Width == 0 || m_Height == 0) {
			    m_LastExtent = m_Context->GetSwapChainExtent();
                m_Width = m_LastExtent.width;
                m_Height = m_LastExtent.height;
            } else {
                m_LastExtent = { m_Width, m_Height };
            }
			RebuildGraph();
		}

		virtual void SetupGraph(RenderGraph& graph) = 0;
		virtual RenderPathType GetType() const = 0;
		virtual uint32_t GetFrameCount() const { return 0; }
		
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
		RenderGraph* GetRenderGraphPtr() { return m_RenderGraph.get(); }
		RenderPathType GetRenderPathType() const { return GetType(); }

		virtual void OnImGui() {}

        void SetViewportSize(uint32_t width, uint32_t height)
        {
            if (width == 0 || height == 0) return;
            if (m_Width != width || m_Height != height)
            {
                m_Width = width;
                m_Height = height;
                m_NeedsRebuild = true;
            }
        }

        virtual void Update()
        {
            // 1. Check window resize (only if no explicit viewport size set)
            if (m_Width == 0 || m_Height == 0)
            {
                auto extent = m_Context->GetSwapChainExtent();
                if (extent.width > 0 && extent.height > 0)
                {
                    if (m_LastExtent.width != extent.width || m_LastExtent.height != extent.height)
                    {
                        vkDeviceWaitIdle(m_Context->GetDevice());
                        m_NeedsRebuild = true;
                        m_LastExtent = extent;
                    }
                }
            }

            // 2. Check if rebuild is needed
            if (m_NeedsRebuild)
            {
                vkDeviceWaitIdle(m_Context->GetDevice());
                RebuildGraph();
                m_NeedsRebuild = false;
            }
        }

		virtual void Render(VkCommandBuffer cmd, uint32_t currentFrame, uint32_t imageIndex, 
							VkDescriptorSet globalDescriptorSet, const std::vector<VkImage>& swapChainImages,
							std::function<void(VkCommandBuffer)> uiDrawCallback = nullptr)
		{
			// 3. Execute Render Graph
			if (m_RenderGraph)
			{
				m_RenderGraph->Execute(cmd, currentFrame, imageIndex, uiDrawCallback);
			}
		}

	protected:
		void RebuildGraph(); // Move implementation to cpp or handle elsewhere

		std::shared_ptr<VulkanContext> m_Context;
		std::shared_ptr<Scene> m_Scene;
		ResourceManager* m_ResourceManager;
		PipelineManager& m_PipelineManager;
		
		std::unique_ptr<RenderGraph> m_RenderGraph;
		VkExtent2D m_LastExtent = { 0, 0 };
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
		bool m_NeedsRebuild = false;
	};

}