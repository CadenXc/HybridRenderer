#include "pch.h"
#include "RenderPath.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Backend/PipelineManager.h"

namespace Chimera
{
RenderPath::RenderPath(std::shared_ptr<VulkanContext> context)
    : m_Context(context)
{
}

RenderPath::~RenderPath()
{
    CH_CORE_INFO("RenderPath: Destructor started. context count: {}",
                 m_Context.use_count());
    m_RenderGraph.reset();
    CH_CORE_INFO("RenderPath: Destructor finished.");
}

void RenderPath::Init()
{
    CH_CORE_INFO("RenderPath: Initializing RenderGraph ({0}x{1})...", m_Width,
                 m_Height);
    m_RenderGraph =
        std::make_unique<RenderGraph>(*m_Context, m_Width, m_Height);
}

VkSemaphore RenderPath::Render(const RenderFrameInfo& frameInfo)
{
	if (!m_Context)
	{
		return VK_NULL_HANDLE;
	}

    VkExtent2D actualExtent = m_Context->GetSwapChainExtent();
    if (m_Width != actualExtent.width || m_Height != actualExtent.height)
    {
        m_Width = actualExtent.width;
        m_Height = actualExtent.height;
        m_NeedsResize = true;
    }

	// 1. Handle resize and lazy initialization
    if (m_NeedsResize || m_NeedsRebuild || !m_RenderGraph)
    {
        CH_CORE_INFO(
            "RenderPath: Rebuilding RenderGraph (Resize: {}, Rebuild: {})...",
            m_NeedsResize, m_NeedsRebuild);

        vkDeviceWaitIdle(m_Context->GetDevice());

        PipelineManager::Get().ClearCache();

        m_RenderGraph =
            std::make_unique<RenderGraph>(*m_Context, m_Width, m_Height);

        m_BenchmarkRecorder.Reset();
        m_LastConsumedTimingSampleId = 0;

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
    scDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                   VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    scDesc.flags = (RGResourceFlags)RGResourceFlagBits::External;

    ResourceState swapchainState{};
    swapchainState.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    swapchainState.access = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    swapchainState.stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    m_RenderGraph->SetExternalResource(RS::RENDER_OUTPUT, scImage, scView,
                                       swapchainState, scDesc);

    m_RenderGraph->Compile();

    VkSemaphore result = m_RenderGraph->Execute(frameInfo.commandBuffer);

    if (m_BenchmarkRecorder.IsRunning())
    {
        const uint64_t sampleId = m_RenderGraph->GetTimingSampleId();

        if (sampleId != 0 && sampleId != m_LastConsumedTimingSampleId)
        {
            m_BenchmarkRecorder.SubmitFrame(m_RenderGraph->GetLatestTimings());

            m_LastConsumedTimingSampleId = sampleId;
        }
    }

    return result;
}

void RenderPath::StartBenchmark(uint32_t warmupFrames,
                                uint32_t captureFrames)
{
    m_BenchmarkRecorder.Start(warmupFrames, captureFrames);

    m_LastConsumedTimingSampleId =
        m_RenderGraph ? m_RenderGraph->GetTimingSampleId() : 0;
}

void RenderPath::ResetBenchmark()
{
    m_BenchmarkRecorder.Reset();

    m_LastConsumedTimingSampleId =
        m_RenderGraph ? m_RenderGraph->GetTimingSampleId() : 0;
}

} // namespace Chimera
