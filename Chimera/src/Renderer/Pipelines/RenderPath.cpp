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
}
