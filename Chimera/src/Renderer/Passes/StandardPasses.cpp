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
    VkClearColorValue clearColor;
};
void AddClearPass(RenderGraph& graph, const std::string& name,
                  const VkClearColorValue& clearColor)
{
    graph.AddPassRaw<ClearData>(
        "Clear_" + name,
        [&](ClearData& data, RenderGraph::PassBuilder& builder)
        {
            data.output = builder.WriteTransfer(name).Format(
                VK_FORMAT_R16G16B16A16_SFLOAT);

            data.clearColor = clearColor;
        },
        [](const ClearData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd)
        {
            VkImageSubresourceRange range{};
            range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            range.baseMipLevel = 0;
            range.levelCount = 1;
            range.baseArrayLayer = 0;
            range.layerCount = 1;

            vkCmdClearColorImage(cmd, reg.GetImage(data.output),
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 &data.clearColor, 1, &range);
        });
}
} // namespace Chimera::StandardPasses
