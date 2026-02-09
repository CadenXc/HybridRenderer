#include "pch.h"
#include "RTShadowAOPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Backend/PipelineManager.h"

namespace Chimera
{
    void RTShadowAOPass::Setup(RenderGraph& graph)
    {
        graph.AddRaytracingPass({
            .Name = "RTShadowAOPass",
            .Dependencies = {
                { .name = RS::Normal,   .type = TransientResourceType::Image, .image = { .type = TransientImageType::SampledImage, .binding = 2 } },
                { .name = RS::Depth,    .type = TransientResourceType::Image, .image = { .type = TransientImageType::SampledImage, .binding = 3 } },
                { .name = RS::Material, .type = TransientResourceType::Image, .image = { .type = TransientImageType::SampledImage, .binding = 4 } }
            },
            .Outputs = {
                { .name = RS::ShadowAO,    .type = TransientResourceType::Image, .image = { .format = VK_FORMAT_R16G16_SFLOAT, .type = TransientImageType::StorageImage, .binding = 0 } },
                { .name = RS::RTOutput,    .type = TransientResourceType::Image, .image = { .format = VK_FORMAT_R16G16B16A16_SFLOAT, .type = TransientImageType::StorageImage, .binding = 1 } }
            },
            .Pipeline = {
                .raygen_shader = "rt_shadow_ao.rgen",
                .miss_shaders = { "miss.rmiss", "shadow.rmiss" },
                .hit_shaders = { { .closest_hit = "closesthit.rchit" } }
            },
            .Callback = [w = m_Width, h = m_Height](ExecuteRaytracingCallback& execute)
            {
                execute("RTShadowAO", [w, h](RaytracingExecutionContext& ctx)
                {
                    ctx.Dispatch(w, h, 1);
                });
            }
        });
    }
}
