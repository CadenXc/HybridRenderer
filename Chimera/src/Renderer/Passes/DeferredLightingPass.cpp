#include "pch.h"
#include "DeferredLightingPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Core/Application.h"

namespace Chimera
{
    void DeferredLightingPass::Setup(RenderGraph& graph)
    {
        GraphicsPassSpecification spec;
        spec.Name = "DeferredLightingPass";
        spec.Dependencies = {
            { .name = RS::Albedo,   .type = TransientResourceType::Image, .image = { .binding = 0 } },
            { .name = RS::Normal,   .type = TransientResourceType::Image, .image = { .binding = 1 } },
            { .name = RS::Material, .type = TransientResourceType::Image, .image = { .binding = 2 } },
            { .name = RS::Depth,    .type = TransientResourceType::Image, .image = { .binding = 3 } },
            { .name = RS::ShadowAO, .type = TransientResourceType::Image, .image = { .binding = 4 } },
            { .name = RS::RTOutput, .type = TransientResourceType::Image, .image = { .binding = 5 } }
        };
        spec.Outputs = {
            TransientResource::Attachment(RS::FinalColor, VK_FORMAT_R8G8B8A8_UNORM)
        };
        spec.Pipelines = { {
            .name = "Deferred",
            .vertex_shader = "fullscreen.vert",
            .fragment_shader = "deferred_lighting.frag",
            .depth_test = false, 
            .depth_write = false,
            .cull_mode = VK_CULL_MODE_NONE
        } };
        spec.Callback = [](ExecuteGraphicsCallback& execute)
        {
            execute("Deferred", [](GraphicsExecutionContext& ctx)
            {
                vkCmdDraw(ctx.GetCommandBuffer(), 3, 1, 0, 0);
            });
        };
        graph.AddGraphicsPass(spec);
    }
}
