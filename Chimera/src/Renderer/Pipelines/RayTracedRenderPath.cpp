#include "pch.h"
#include "RayTracedRenderPath.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Backend/VulkanContext.h"

namespace Chimera {

    RayTracedRenderPath::RayTracedRenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager, PipelineManager& pipelineManager)
        : RenderPath(context, scene, resourceManager, pipelineManager)
    {
        m_RaytracePass = std::make_unique<RaytracePass>(m_Width, m_Height);
    }

    void RayTracedRenderPath::Render(const RenderFrameInfo& frameInfo) 
    {
        if (m_NeedsRebuild || !m_RenderGraph) {
            vkDeviceWaitIdle(m_Context->GetDevice());
            if (!m_RenderGraph) Init();
            
            m_RaytracePass->Setup(*m_RenderGraph);
            m_RenderGraph->AddBlitPass("FinalBlit", RS::RTOutput, RS::RENDER_OUTPUT, VK_FORMAT_R16G16B16A16_SFLOAT, m_Context->GetSwapChainImageFormat());
            m_RenderGraph->Build();
            m_NeedsRebuild = false;
        }

        m_RenderGraph->Execute(frameInfo.commandBuffer, frameInfo.frameIndex, frameInfo.imageIndex);
    }

}