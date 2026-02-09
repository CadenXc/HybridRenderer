#include "pch.h"
#include "RaytracePass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Backend/PipelineManager.h"

namespace Chimera
{
    void RaytracePass::Setup(RenderGraph& graph)
    {
        graph.AddRaytracingPass({
            .Name = "RaytracePass",
            .Dependencies = {
                { .name = RS::RTOutput, .type = TransientResourceType::Image, .image = { .format = VK_FORMAT_R16G16B16A16_SFLOAT, .type = TransientImageType::StorageImage, .binding = 0 } }
            },
            .Outputs = {},
            .Pipeline = {
                .raygen_shader = "raygen.rgen",
                .miss_shaders = { "miss.rmiss" },
                .hit_shaders = { { .closest_hit = "closesthit.rchit" } }
            },
            .Callback = [w = m_Width, h = m_Height](ExecuteRaytracingCallback& execute)
            {
                execute("RaytracePass", [w, h](RaytracingExecutionContext& ctx)
                {
                    struct
                    {
                        glm::vec4 clear;
                        int sky;
                    } push;
                    push.clear = {0.1f, 0.2f, 0.4f, 1.0f};
                    push.sky = -1;
                    vkCmdPushConstants(ctx.GetCommandBuffer(), ctx.GetPipeline().layout, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR, 0, sizeof(push), &push);
                    ctx.Dispatch(w, h, 1);
                });
            }
        });
    }
}
