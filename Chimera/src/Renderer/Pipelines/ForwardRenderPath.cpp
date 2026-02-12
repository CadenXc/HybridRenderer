#include "pch.h"
#include "ForwardRenderPath.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"

namespace Chimera
{
    ForwardRenderPath::ForwardRenderPath(VulkanContext& context, std::shared_ptr<Scene> scene)
        : RenderPath(std::shared_ptr<VulkanContext>(&context, [](VulkanContext*){}), scene)
    {
    }

    ForwardRenderPath::~ForwardRenderPath()
    {
    }

    void ForwardRenderPath::Render(const RenderFrameInfo& frameInfo)
    {
        if (m_NeedsResize) {
            m_RenderGraph = std::make_unique<RenderGraph>(*m_Context, m_Width, m_Height);
            m_NeedsResize = false;
        }

        m_RenderGraph->Reset();
        
        struct ForwardData {};
        m_RenderGraph->AddPass<ForwardData>("ForwardPass",
            [&](ForwardData& data, RenderGraph::PassBuilder& builder) {
                builder.Write(RS::RENDER_OUTPUT);
            },
            [=](const ForwardData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd) {
            }
        );

        m_RenderGraph->Compile();
        m_RenderGraph->Execute(frameInfo.commandBuffer);
    }
}