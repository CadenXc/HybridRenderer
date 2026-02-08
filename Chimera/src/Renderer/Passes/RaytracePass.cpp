#include "pch.h"
#include "RaytracePass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraphCommon.h"

namespace Chimera {

    void RaytracePass::Setup(RenderGraph& graph)
    {
        graph.AddRaytracingPass({
            .Name = "RaytracePass",
            .Dependencies = {
                TransientResource::AccelerationStructure(RS::SceneAS),
                TransientResource::Buffer(RS::MaterialBuffer),
                TransientResource::Sampler(RS::TextureArray, 1024)
            },
            .Outputs = {
                TransientResource::StorageImage(RS::RTOutput, VK_FORMAT_R16G16B16A16_SFLOAT)
            },
            .Pipeline = {
                .raygen_shader = "raygen.rgen",
                .miss_shaders = { "miss.rmiss" },
                .hit_shaders = { { .closest_hit = "closesthit.rchit" } }
            },
            .Callback = [w = m_Width, h = m_Height](ExecuteRaytracingCallback& execute) {
                execute("Raytrace", [w, h](RaytracingExecutionContext& ctx) {
                    ctx.Dispatch(w, h, 1);
                });
            },
            .ShaderLayout = "RaytraceLayout"
        });
    }

}
