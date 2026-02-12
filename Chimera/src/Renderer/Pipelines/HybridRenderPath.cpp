#include "pch.h"
#include "HybridRenderPath.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Passes/GBufferPass.h"
#include "Renderer/Passes/RTShadowAOPass.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"
#include "Renderer/Backend/Renderer.h"
#include "Core/Application.h"

namespace Chimera
{
    HybridRenderPath::HybridRenderPath(VulkanContext& context, std::shared_ptr<Scene> scene)
        : RenderPath(std::shared_ptr<VulkanContext>(&context, [](VulkanContext*){}), scene)
    {
    }

    HybridRenderPath::~HybridRenderPath()
    {
        m_RenderGraph.reset();
    }

    void HybridRenderPath::Render(const RenderFrameInfo& frameInfo)
    {
        if (m_NeedsResize)
        {
            if (!m_Context) return;
            m_RenderGraph = std::make_unique<RenderGraph>(*m_Context, m_Width, m_Height);
            m_NeedsResize = false;
        }

        if (!m_RenderGraph) return;
        m_RenderGraph->Reset();

        // 1. G-Buffer
        if (m_Scene) GBufferPass::AddToGraph(*m_RenderGraph, m_Scene);

        // 2. RT Shadow/AO
        if (m_Scene) RTShadowAOPass::AddToGraph(*m_RenderGraph, m_Scene);

        // 4. Composition
        struct CompData { RGResourceHandle albedo, shadow, output; };
        m_RenderGraph->AddPass<CompData>("Composition",
            [&](CompData& data, RenderGraph::PassBuilder& builder) {
                data.albedo = builder.Read(RS::Albedo);
                data.shadow = builder.Read(RS::ShadowAO);
                data.output = builder.Write(RS::FinalColor, VK_FORMAT_R16G16B16A16_SFLOAT);
            },
            [=](const CompData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd) {
                GraphicsExecutionContext ctx(reg.graph, reg.pass, cmd);
                ctx.DrawMeshes({ "Composition", "common/fullscreen.vert", "postprocess/composition.frag", false, false }, nullptr);
            }
        );

        // 5. Final Output
        struct FinalData { RGResourceHandle src; };
        m_RenderGraph->AddPass<FinalData>("FinalBlit",
            [&](FinalData& data, RenderGraph::PassBuilder& builder) {
                data.src = builder.Read(RS::FinalColor);
                builder.Write(RS::RENDER_OUTPUT);
            },
            [=](const FinalData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd) {
                GraphicsExecutionContext ctx(reg.graph, reg.pass, cmd);
                ctx.DrawMeshes({ "FinalBlit", "common/fullscreen.vert", "postprocess/blit.frag", false, false }, nullptr);
            }
        );

        m_RenderGraph->Compile();
        m_RenderGraph->Execute(frameInfo.commandBuffer);
    }
}
