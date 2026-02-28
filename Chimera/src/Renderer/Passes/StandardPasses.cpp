#include "pch.h"
#include "StandardPasses.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"

namespace Chimera
{
    void StandardPasses::AddLinearizeDepthPass(RenderGraph& graph)
    {
        if (!graph.ContainsImage(RS::Depth)) return;

        struct DepthData { RGResourceHandle depth; RGResourceHandle output; };
        graph.AddPass<DepthData>("LinearizeDepth",
            [&](DepthData& data, RenderGraph::PassBuilder& builder)
            {
                data.depth = builder.Read(RS::Depth);
                data.output = builder.Write("DepthLinear").Format(VK_FORMAT_R8G8B8A8_UNORM);
            },
            [=](const DepthData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd)
            {
                GraphicsExecutionContext ctx(reg.graph, reg.pass, cmd);
                ctx.DrawMeshes({ "LinearizeDepth", "common/fullscreen.vert", "postprocess/linearize_depth.frag", false, false }, nullptr);
            }
        );
    }
}
