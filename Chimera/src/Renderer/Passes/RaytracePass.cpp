#include "pch.h"
#include "RaytracePass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RaytracingExecutionContext.h"

namespace Chimera::RaytracePass
{
    struct PassData { RGResourceHandle output; };

    void AddToGraph(RenderGraph& graph, std::shared_ptr<Scene> scene, bool useAlphaTest)
    {
        if (!scene) return;

        graph.AddPass<PassData>("RaytracePass",
            [&](PassData& data, RenderGraph::PassBuilder& builder)
            {
                data.output = builder.WriteStorage(RS::FinalColor).Format(VK_FORMAT_R16G16B16A16_SFLOAT);
            },
            [=](const PassData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd)
            {
                RaytracingExecutionContext ctx(reg.graph, reg.pass, cmd);
                
                RaytracingPipelineDescription desc{};
                desc.raygen_shader = "raytracing/raytrace.rgen";
                desc.miss_shaders = { "raytracing/miss.rmiss" };
                desc.hit_shaders = { { "raytracing/closesthit.rchit", "raytracing/shadow.rahit" } };
                
                ctx.BindPipeline(desc);

                int alphaTest = useAlphaTest ? 1 : 0;
                ctx.PushConstants(VK_SHADER_STAGE_RAYGEN_BIT_KHR, alphaTest);

                ctx.TraceRays(reg.graph.GetWidth(), reg.graph.GetHeight());
            }
        );
    }
}
