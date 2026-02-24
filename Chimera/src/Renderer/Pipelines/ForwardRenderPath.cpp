#include "pch.h"
#include "ForwardRenderPath.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Passes/ForwardPass.h"
#include "Renderer/Passes/StandardPasses.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"

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
            if (!m_Context) return VK_NULL_HANDLE;
            m_RenderGraph = std::make_unique<RenderGraph>(*m_Context, m_Width, m_Height);
            m_NeedsResize = false;
        }

        if (!m_RenderGraph) return VK_NULL_HANDLE;
        
        m_RenderGraph->Reset();

        auto scene = GetSceneShared();

        // 1. Main Forward Pass (Directly to Final Output)
        if (scene)
        {
            ForwardPass::AddToGraph(*m_RenderGraph, scene);
        }

        // 2. Linearize Depth (For Debug Visualization Only)
        StandardPasses::AddLinearizeDepthPass(*m_RenderGraph);

        // 3. Skybox Pass (If implemented in your version)
        // StandardPasses::AddSkyboxPass(*m_RenderGraph, scene.get());

        m_RenderGraph->Compile();
        return m_RenderGraph->Execute(frameInfo.commandBuffer);
    }
}
