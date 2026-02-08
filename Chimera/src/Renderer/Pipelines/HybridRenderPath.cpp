#include "pch.h"
#include "HybridRenderPath.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Backend/VulkanContext.h"

namespace Chimera {

    HybridRenderPath::HybridRenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager, PipelineManager& pipelineManager)
        : RenderPath(context, scene, resourceManager, pipelineManager)
    {
    }

    void HybridRenderPath::Render(const RenderFrameInfo& frameInfo) 
    {
        if (m_NeedsRebuild || !m_RenderGraph) {
            vkDeviceWaitIdle(m_Context->GetDevice());
            if (!m_RenderGraph) Init();
            
            GBufferPass gbuffer(m_Scene);
            gbuffer.Setup(*m_RenderGraph);
            
            RTShadowAOPass shadow(m_Context, m_Width, m_Height);
            shadow.Setup(*m_RenderGraph);

            DeferredLightingPass deferred(m_Scene, m_Width, m_Height);
            deferred.Setup(*m_RenderGraph);

            m_RenderGraph->Build();
            m_NeedsRebuild = false;
        }

        m_RenderGraph->Execute(frameInfo.commandBuffer, frameInfo.frameIndex, frameInfo.imageIndex);
    }

}
