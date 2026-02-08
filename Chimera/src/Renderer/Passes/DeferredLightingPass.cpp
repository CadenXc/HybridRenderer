#include "pch.h"
#include "DeferredLightingPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"

namespace Chimera {

    void DeferredLightingPass::Setup(RenderGraph& graph)
    {
        graph.AddGraphicsPass({
            .Name = "DeferredLightingPass",
            .Dependencies = {
                TransientResource::Image(RS::Albedo, VK_FORMAT_R8G8B8A8_UNORM),
                TransientResource::Image(RS::Normal, VK_FORMAT_R16G16B16A16_SFLOAT),
                TransientResource::Image(RS::Material, VK_FORMAT_R8G8B8A8_UNORM),
                TransientResource::Image(RS::Depth, VK_FORMAT_D32_SFLOAT)
            },
            .Outputs = { TransientResource::Attachment(RS::RENDER_OUTPUT, VK_FORMAT_B8G8R8A8_UNORM) },
            .Pipelines = { { "Deferred", "fullscreen.vert", "deferred_lighting.frag" } },
            .Callback = [](ExecuteGraphicsCallback& execute) {
                execute("Deferred", [](GraphicsExecutionContext& ctx) {
                    ctx.DrawFullscreenQuad();
                });
            }
        });
    }

}