#include "pch.h"
#include "HybridRenderPath.h"
#include "Scene/Scene.h"
#include "Renderer/Passes/GBufferPass.h"
#include "Renderer/Passes/RTShadowAOPass.h"
#include "Renderer/Passes/DeferredLightingPass.h"
#include "Renderer/Passes/LinearizeDepthPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Backend/VulkanContext.h"

namespace Chimera
{
    HybridRenderPath::HybridRenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene)
        : RenderPath(context, scene)
    {
    }

    void HybridRenderPath::Render(const RenderFrameInfo& frameInfo) 
    {
        if (m_NeedsRebuild || !m_RenderGraph)
        {
            CH_CORE_INFO("HybridRenderPath: Building/Rebuilding RenderGraph...");
            vkDeviceWaitIdle(m_Context->GetDevice());
            
            // CRITICAL: Build TLAS before graph setup to ensure handles are valid
            m_Scene->BuildTLAS();

            Init();
            
            auto& graph = *m_RenderGraph;
            CH_CORE_INFO("HybridRenderPath: Setting up GBufferPass...");
            GBufferPass gbuffer(m_Scene);
            gbuffer.Setup(graph);
            
            CH_CORE_INFO("HybridRenderPath: Setting up RTShadowAOPass...");
            RTShadowAOPass shadow(m_Context, m_Width, m_Height);
            shadow.Setup(graph);

            CH_CORE_INFO("HybridRenderPath: Setting up DeferredLightingPass...");
            DeferredLightingPass deferred(m_Scene, m_Width, m_Height);
            deferred.Setup(graph);

            CH_CORE_INFO("HybridRenderPath: Setting up LinearizeDepthPass...");
            LinearizeDepthPass linearize;
            linearize.Setup(graph);

            CH_CORE_INFO("HybridRenderPath: Finalizing RenderGraph build...");
            graph.Build();
            m_NeedsRebuild = false;
            CH_CORE_INFO("HybridRenderPath: RenderGraph ready.");
        }

        m_RenderGraph->Execute(frameInfo.commandBuffer, frameInfo.frameIndex, frameInfo.imageIndex);
    }
}
