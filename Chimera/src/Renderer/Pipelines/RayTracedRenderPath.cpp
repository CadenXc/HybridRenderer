#include "pch.h"
#include "RayTracedRenderPath.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"

namespace Chimera
{
    RayTracedRenderPath::RayTracedRenderPath(VulkanContext& context, std::shared_ptr<Scene> scene)
        : RenderPath(std::shared_ptr<VulkanContext>(&context, [](VulkanContext*){}), scene)
    {
    }

    RayTracedRenderPath::~RayTracedRenderPath()
    {
    }

    void RayTracedRenderPath::Render(const RenderFrameInfo& frameInfo)
    {
        if (m_NeedsResize) {
            m_RenderGraph = std::make_unique<RenderGraph>(*m_Context, m_Width, m_Height);
            m_NeedsResize = false;
        }

        m_RenderGraph->Reset();

        struct RTData {};
        m_RenderGraph->AddPass<RTData>("RTPass",
            [&](RTData& data, RenderGraph::PassBuilder& builder) {
                builder.Write(RS::RENDER_OUTPUT);
            },
            [=](const RTData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd) {
            }
        );

        m_RenderGraph->Compile();
        m_RenderGraph->Execute(frameInfo.commandBuffer);
    }
}