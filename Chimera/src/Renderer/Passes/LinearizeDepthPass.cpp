#include "pch.h"
#include "LinearizeDepthPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Core/Application.h"
#include "Renderer/RenderState.h"

namespace Chimera
{
    void LinearizeDepthPass::Setup(RenderGraph& graph)
    {
        uint32_t w = graph.GetWidth();
        uint32_t h = graph.GetHeight();

        graph.AddComputePass({
            .Name = "LinearizeDepthPass",
            .Dependencies = {
                { .name = RS::Depth, .type = TransientResourceType::Image, .image = { .type = TransientImageType::SampledImage, .binding = 0 } }
            },
            .Outputs = {
                { .name = RS::LinearDepth, .type = TransientResourceType::Image, .image = { .format = VK_FORMAT_R8G8B8A8_UNORM, .type = TransientImageType::StorageImage, .binding = 1 } }
            },
            .Pipeline = { 
                .kernels = { { "LinearizeDepth", "linearize_depth.comp" } }
            },
            .Callback = [w, h](ComputeExecutionContext& ctx)
            {
                float scale = Application::Get().GetDepthScale();
                ctx.Bind("LinearizeDepth");
                ctx.PushConstants(scale);
                ctx.Dispatch((w + 15) / 16, (h + 15) / 16, 1);
            }
        });
    }
}
