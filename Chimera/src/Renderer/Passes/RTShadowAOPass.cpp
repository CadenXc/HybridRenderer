#include "pch.h"
#include "RTShadowAOPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"

namespace Chimera {

    void RTShadowAOPass::Setup(RenderGraph& graph)
    {
        graph.AddRaytracingPass({
            .Name = "RTShadowAOPass",
            .Dependencies = {
                TransientResource::AccelerationStructure(RS::SceneAS),
                TransientResource::Buffer(RS::MaterialBuffer),
                TransientResource::Buffer(RS::InstanceBuffer),
                TransientResource::Sampler(RS::TextureArray, 1024),
                TransientResource::Image(RS::Normal, VK_FORMAT_R16G16B16A16_SFLOAT),
                TransientResource::Image(RS::Depth, VK_FORMAT_D32_SFLOAT)
            },
            .Outputs = {
                TransientResource::StorageImage(RS::ShadowAO, VK_FORMAT_R16G16_SFLOAT),
                TransientResource::StorageImage(RS::Reflections, VK_FORMAT_R16G16B16A16_SFLOAT)
            },
            .Pipeline = {
                .raygen_shader = "rt_shadow_ao.rgen",
                .miss_shaders = { "shadow.rmiss" },
                .hit_shaders = { { .closest_hit = "closesthit.rchit" } }
            },
            .Callback = [w = m_Width, h = m_Height](ExecuteRaytracingCallback& execute) {
                execute("RTShadowAO", [w, h](RaytracingExecutionContext& ctx) {
                    ctx.Dispatch(w, h, 1);
                });
            }
        });
    }

}