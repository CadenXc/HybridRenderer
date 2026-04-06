#include "pch.h"
#include "SkyboxPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"
#include "Renderer/Graph/RenderGraph.h"

namespace Chimera
{
void SkyboxPass::Setup(SkyboxPassData& data, RenderGraph::PassBuilder& builder)
{
    data.output =
        builder.Write(RS::FinalColor).Format(VK_FORMAT_R16G16B16A16_SFLOAT);
}

void SkyboxPass::Execute(const SkyboxPassData& data, RenderGraphRegistry& reg,
                         VkCommandBuffer cmd)
{
    GraphicsExecutionContext ctx(reg.graph, reg.pass, cmd);

    GraphicsPipelineDescription desc;
    desc.name = "Skybox";
    desc.vertex_shader = "Fullscreen_Vert";
    desc.fragment_shader = "Skybox_Frag";
    desc.depth_test = false;
    desc.depth_write = false;

    ctx.BindPipeline(desc);

        // Use a full-screen triangle to draw skybox
    vkCmdDraw(cmd, 3, 1, 0, 0);
}
} // namespace Chimera
