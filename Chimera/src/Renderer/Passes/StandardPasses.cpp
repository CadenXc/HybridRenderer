#include "pch.h"
#include "StandardPasses.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"

namespace Chimera::StandardPasses
{
struct DepthData
{
    RGResourceHandle depth;
    RGResourceHandle output;
};

void AddLinearizeDepthPass(RenderGraph& graph)
{
    if (!graph.ContainsImage(RS::Depth)) return;

    graph.AddPassRaw<DepthData>(
        "LinearizeDepth",
        [&](DepthData& data, RenderGraph::PassBuilder& builder)
        {
            data.depth = builder.Read(RS::Depth);
            data.output =
                builder.Write("DepthLinear").Format(VK_FORMAT_R8G8B8A8_UNORM);
        },
        [](const DepthData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd)
        {
            GraphicsExecutionContext ctx(reg.graph, reg.pass, cmd);
            // ctx.DrawMeshes({ "LinearizeDepth", "common/fullscreen.vert",
            // "postprocess/linearize_depth.frag", false, false }, nullptr);
        });
}

struct SkyboxData
{
    RGResourceHandle output;
};
void AddSkyboxPass(RenderGraph& graph)
{
    graph.AddPassRaw<SkyboxData>(
        "SkyboxPass",
        [&](SkyboxData& data, RenderGraph::PassBuilder& builder)
        {
            data.output = builder.Write(RS::FinalColor)
                              .Format(VK_FORMAT_R16G16B16A16_SFLOAT);
        },
        [](const SkyboxData& data, RenderGraphRegistry& reg,
           VkCommandBuffer cmd)
        {
            GraphicsExecutionContext ctx(reg.graph, reg.pass, cmd);
            GraphicsPipelineDescription desc{"Skybox", "Fullscreen_Vert",
                                             "Skybox_Frag", false, false};
            ctx.BindPipeline(desc);
            ctx.DrawMeshes(desc, nullptr);
        });
}

struct ClearData
{
    RGResourceHandle output;
};
void AddClearPass(RenderGraph& graph, const std::string& name,
                  const VkClearColorValue& clearColor)
{
    graph.AddPassRaw<ClearData>(
        "Clear_" + name,
        [&](ClearData& data, RenderGraph::PassBuilder& builder)
        {
            data.output = builder.Write(name)
                              .Format(VK_FORMAT_R16G16B16A16_SFLOAT)
                              .Clear(clearColor);
        },
        [](const ClearData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd)
        {
            // LoadOpClear is handled by RenderGraph
        });
}
} // namespace Chimera::StandardPasses
