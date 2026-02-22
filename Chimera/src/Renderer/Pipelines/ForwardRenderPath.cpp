#include "pch.h"
#include "ForwardRenderPath.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Passes/ForwardPass.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"
#include "Renderer/Backend/Renderer.h"

namespace Chimera
{
    ForwardRenderPath::ForwardRenderPath(VulkanContext& context)
        : RenderPath(std::shared_ptr<VulkanContext>(&context, [](VulkanContext*){}))
    {
    }

    ForwardRenderPath::~ForwardRenderPath()
    {
        m_RenderGraph.reset();
    }

    VkSemaphore ForwardRenderPath::Render(const RenderFrameInfo& frameInfo)
    {
        if (m_NeedsResize)
        {
            if (!m_Context)
            {
                return VK_NULL_HANDLE;
            }
            m_RenderGraph = std::make_unique<RenderGraph>(*m_Context, m_Width, m_Height);
            m_NeedsResize = false;
        }

        if (!m_RenderGraph)
        {
            return VK_NULL_HANDLE;
        }
        m_RenderGraph->Reset();

        // 1. Main Forward Pass
        auto scene = GetSceneShared();
        if (scene)
        {
            ForwardPass::AddToGraph(*m_RenderGraph, scene);
        }

        // 2. Final Blit to swapchain
        struct FinalData { RGResourceHandle src; };
        m_RenderGraph->AddPass<FinalData>("FinalBlit",
            [&](FinalData& data, RenderGraph::PassBuilder& builder)
            {
                // Use FinalColor which is typically what ForwardPass writes to
                data.src = builder.Read(RS::FinalColor);
                builder.Write(RS::RENDER_OUTPUT);
            },
            [=](const FinalData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd)
            {
                GraphicsExecutionContext ctx(reg.graph, reg.pass, cmd);
                ctx.DrawMeshes({ "FinalBlit", "common/fullscreen.vert", "postprocess/blit.frag", false, false }, nullptr);
            }
        );

        m_RenderGraph->Compile();
        return m_RenderGraph->Execute(frameInfo.commandBuffer);
    }
}
