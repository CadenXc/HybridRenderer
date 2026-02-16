#include "pch.h"
#include "ForwardPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"
#include "Scene/Scene.h"

namespace Chimera
{
    void ForwardPass::AddToGraph(RenderGraph& graph, std::shared_ptr<Scene> scene)
    {
        struct PassData { RGResourceHandle output, depth; };

        graph.AddPass<PassData>("ForwardPass",
            [&](PassData& data, RenderGraph::PassBuilder& builder) {
                data.output = builder.Write(RS::FinalColor, VK_FORMAT_R16G16B16A16_SFLOAT);
                data.depth  = builder.Write(RS::Depth, VK_FORMAT_D32_SFLOAT);
            },
            [=](const PassData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd) {
                GraphicsExecutionContext ctx(reg.graph, reg.pass, cmd);
                ctx.DrawMeshes({ "Forward", "forward/forward.vert", "forward/forward.frag", true, true }, scene.get());
            }
        );
    }
}
