#include "pch.h"
#include "PostProcessPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"

namespace Chimera
{
PostProcessPass::PostProcessPass(const std::string& inputName)
    : m_InputName(inputName)
{
}

void PostProcessPass::Setup(PassData& data, RenderGraph::PassBuilder& builder)
{
    data.input = builder.Read(m_InputName);
    data.output = builder.Write(RS::RENDER_OUTPUT);
}

void PostProcessPass::Execute(const PassData& data, RenderGraphRegistry& reg,
                              VkCommandBuffer cmd)
{
    GraphicsExecutionContext ctx(reg.graph, reg.pass, cmd);

    ctx.SetViewport(0, 0, (float)reg.graph.GetWidth(),
                    (float)reg.graph.GetHeight());
    ctx.SetScissor(0, 0, reg.graph.GetWidth(), reg.graph.GetHeight());

    GraphicsPipelineDescription desc{"PostProcess",
                                     "common/fullscreen.vert",
                                     "postprocess/postprocess.frag",
                                     false,
                                     false,
                                     (VkCompareOp)0,
                                     VK_CULL_MODE_NONE};

    ctx.BindPipeline(desc);
    ctx.DrawMeshes(desc, nullptr);
}
} // namespace Chimera
