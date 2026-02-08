#include "pch.h"
#include "ForwardRenderPath.h"
#include "Renderer/Passes/ForwardPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Backend/VulkanContext.h"

namespace Chimera {

    ForwardRenderPath::ForwardRenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager, PipelineManager& pipelineManager)
        : RenderPath(context, scene, resourceManager, pipelineManager)
    {
    }

    ForwardRenderPath::~ForwardRenderPath() {}

    void ForwardRenderPath::Render(const RenderFrameInfo& frameInfo) 
    {
        if (m_NeedsRebuild || !m_RenderGraph) {
            vkDeviceWaitIdle(m_Context->GetDevice());
            m_RenderGraph = std::make_unique<RenderGraph>(*m_Context, *m_ResourceManager, m_PipelineManager, m_Width, m_Height);
            
            ForwardPass forward(m_Scene);
            forward.Setup(*m_RenderGraph);

            m_RenderGraph->AddBlitPass("FinalBlit", RS::FinalColor, RS::RENDER_OUTPUT, VK_FORMAT_B8G8R8A8_UNORM, m_Context->GetSwapChainImageFormat());

            m_RenderGraph->Build();
            m_NeedsRebuild = false;
        }

        m_RenderGraph->Execute(frameInfo.commandBuffer, frameInfo.frameIndex, frameInfo.imageIndex);
    }

}
