#pragma once

#include "pch.h"
#include "Renderer/ChimeraCommon.h"
#include <memory>
#include <vector>
#include <functional>

#include "Renderer/Resources/ResourceManager.h"
#include "Scene/Scene.h"
#include "Renderer/Graph/RenderGraph.h"

namespace Chimera
{
    class RenderPath
    {
    public:
        RenderPath(std::shared_ptr<VulkanContext> context);
        virtual ~RenderPath();

        virtual void Init();
        
        // [Template Method] Finalized to ensure consistent lifecycle management
        virtual VkSemaphore Render(const RenderFrameInfo& frameInfo) final;

        void SetViewportSize(uint32_t width, uint32_t height)
        {
            m_Width = width;
            m_Height = height;
            m_NeedsResize = true;
        }
        virtual RenderPathType GetType() const = 0;

        virtual void OnImGui()
        {
        }
        virtual void OnSceneUpdated()
        {
            m_NeedsRebuild = true;
        }

        // --- Optimized [NEW] ---
        Scene* GetScene() const { return ResourceManager::Get().GetActiveScene(); }
        std::shared_ptr<Scene> GetSceneShared() const { return ResourceManager::Get().GetActiveSceneShared(); }

        RenderGraph& GetRenderGraph() const
        {
            return *m_RenderGraph;
        }

    protected:
        // Pure virtual hook for specific render path logic
        virtual void BuildGraph(RenderGraph& graph, std::shared_ptr<Scene> scene) = 0;

        std::shared_ptr<VulkanContext> m_Context;

        mutable std::unique_ptr<RenderGraph> m_RenderGraph;
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        bool m_NeedsRebuild = true;
        bool m_NeedsResize = false;
    };

}
