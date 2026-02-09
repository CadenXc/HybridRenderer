#pragma once

#include "pch.h"
#include "Renderer/ChimeraCommon.h"
#include <memory>
#include <vector>
#include <functional>

namespace Chimera
{
    class RenderPath
   {
    public:
        RenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene);
        virtual ~RenderPath();

        virtual void Init();
        virtual void Render(const RenderFrameInfo& frameInfo) = 0;

        void SetViewportSize(uint32_t width, uint32_t height) { m_Width = width; m_Height = height; m_NeedsRebuild = true; }
        virtual RenderPathType GetType() const = 0;
        
        virtual void OnImGui() {}
        virtual void OnSceneUpdated() { m_NeedsRebuild = true; }
        RenderGraph& GetRenderGraph() const { return *m_RenderGraph; }

    protected:
        std::shared_ptr<VulkanContext> m_Context;
        std::shared_ptr<Scene> m_Scene;
        
        mutable std::unique_ptr<RenderGraph> m_RenderGraph;
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        bool m_NeedsRebuild = true;
    };

}