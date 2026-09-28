#include "pch.h"
#include "StandardPasses.h"
#include "Renderer/Graph/RenderGraph.h"

namespace Chimera::StandardPasses
{
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
