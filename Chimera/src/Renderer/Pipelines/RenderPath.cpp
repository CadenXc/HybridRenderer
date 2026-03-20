#include "pch.h"
#include "RenderPath.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"

namespace Chimera
{
    RenderPath::RenderPath(std::shared_ptr<VulkanContext> context)
        : m_Context(context)
    {
    }

    RenderPath::~RenderPath()
    {
        CH_CORE_INFO("RenderPath: Destructor started. context count: {}", m_Context.use_count());
        m_RenderGraph.reset();
        CH_CORE_INFO("RenderPath: Destructor finished.");
    }

    void RenderPath::Init()
    {
        CH_CORE_INFO("RenderPath: Initializing RenderGraph ({0}x{1})...", m_Width, m_Height);
        m_RenderGraph = std::make_unique<RenderGraph>(*m_Context, m_Width, m_Height);
    }

    VkSemaphore RenderPath::Render(const RenderFrameInfo& frameInfo)
    {
        // 1. Handle resize and lazy initialization
        if (m_NeedsResize || m_NeedsRebuild || !m_RenderGraph)
        {
            if (!m_Context)
            {
                return VK_NULL_HANDLE;
            }

            CH_CORE_INFO("RenderPath: Rebuilding RenderGraph (Resize: {}, Rebuild: {})...", m_NeedsResize, m_NeedsRebuild);
            
            // [FIX] CRITICAL: Wait for GPU to finish work before destroying old RenderGraph
            // Otherwise we might destroy image views currently in use by active descriptor sets.
            vkDeviceWaitIdle(m_Context->GetDevice());

            // Re-creating the RenderGraph forces old resources (and history) to be destroyed
            m_RenderGraph = std::make_unique<RenderGraph>(*m_Context, m_Width, m_Height);
            
            m_NeedsResize = false;
            m_NeedsRebuild = false;
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

        // 5. Set Swapchain as RENDER_OUTPUT
        uint32_t imageIndex = frameInfo.imageIndex;
        auto swapchain = m_Context->GetSwapchain();
        VkImage scImage = swapchain->GetImages()[imageIndex];
        VkImageView scView = swapchain->GetImageViews()[imageIndex];
        
        ImageDescription scDesc{};
        scDesc.width = m_Width;
        scDesc.height = m_Height;
        scDesc.format = m_Context->GetSwapChainImageFormat();
        scDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        scDesc.flags = (RGResourceFlags)RGResourceFlagBits::External;

        m_RenderGraph->SetExternalResource(RS::RENDER_OUTPUT, scImage, scView, VK_IMAGE_LAYOUT_UNDEFINED, scDesc);

        // 6. Compile and execute
        m_RenderGraph->Compile();
        return m_RenderGraph->Execute(frameInfo.commandBuffer);
    }
}
