#include "pch.h"
#include "ForwardRenderPath.h"
#include "Renderer/Passes/ForwardPass.h"
#include "Renderer/Passes/LinearizeDepthPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Backend/VulkanContext.h"

namespace Chimera
{
    ForwardRenderPath::ForwardRenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene)
        : RenderPath(context, scene)
    {
    }

    ForwardRenderPath::~ForwardRenderPath()
    {
    }

    void ForwardRenderPath::Render(const RenderFrameInfo& frameInfo) 
    {
        if (m_NeedsRebuild || !m_RenderGraph)
        {
            vkDeviceWaitIdle(m_Context->GetDevice());
            Init(); // Properly re-initialize or create
            
            ForwardPass forward(m_Scene);
            forward.Setup(*m_RenderGraph);

            LinearizeDepthPass linearize;
            linearize.Setup(*m_RenderGraph);

            m_RenderGraph->AddBlitPass("FinalBlit", RS::FinalColor, RS::RENDER_OUTPUT, VK_FORMAT_B8G8R8A8_UNORM, m_Context->GetSwapChainImageFormat());

            m_RenderGraph->Build();
            m_NeedsRebuild = false;
            m_NeedsResize = false;
        }
        else if (m_NeedsResize)
        {
            vkDeviceWaitIdle(m_Context->GetDevice());
            m_RenderGraph->Resize(m_Width, m_Height);
            m_NeedsResize = false;
        }

        m_RenderGraph->Execute(frameInfo.commandBuffer, frameInfo.frameIndex, frameInfo.imageIndex);
    }
}
