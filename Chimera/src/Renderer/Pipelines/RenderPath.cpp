#include "pch.h"
#include "RenderPath.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Graph/RenderGraph.h"

namespace Chimera
{
    RenderPath::RenderPath(std::shared_ptr<VulkanContext> context)
        : m_Context(context)
    {
    }

    RenderPath::~RenderPath()
    {
        CH_CORE_INFO("RenderPath: Destroying...");
        m_RenderGraph.reset();
    }

    void RenderPath::Init()
    {
        CH_CORE_INFO("RenderPath: Initializing RenderGraph ({0}x{1})...", m_Width, m_Height);
        m_RenderGraph = std::make_unique<RenderGraph>(*m_Context, m_Width, m_Height);
    }

    VkSemaphore RenderPath::Render(const RenderFrameInfo& frameInfo)
    {
        // 1. Handle resize and lazy initialization
        if (m_NeedsResize || !m_RenderGraph)
        {
            if (!m_Context) return VK_NULL_HANDLE;
            m_RenderGraph = std::make_unique<RenderGraph>(*m_Context, m_Width, m_Height);
            m_NeedsResize = false;
        }

        if (!m_RenderGraph)
        {
            return VK_NULL_HANDLE;
        }

        // 2. Prepare graph for new frame
        m_RenderGraph->Reset();

        // 3. Obtain scene data
        auto scene = GetSceneShared();

        // 4. Build graph using subclass-specific logic
        if (scene)
        {
            BuildGraph(*m_RenderGraph, scene);
        }

        // 5. Compile and execute
        m_RenderGraph->Compile();
        return m_RenderGraph->Execute(frameInfo.commandBuffer);
    }
}
