#include "pch.h"
#include "RaytracePass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/RaytracingExecutionContext.h"
#include "Scene/Scene.h"

namespace Chimera
{
    void RaytracePass::AddToGraph(RenderGraph& graph, std::shared_ptr<Scene> scene)
    {
        struct PassData { RGResourceHandle output; };

        graph.AddPass<PassData>("RaytracePass",
            [&](PassData& data, RenderGraph::PassBuilder& builder) {
                data.output = builder.WriteStorage(RS::FinalColor, VK_FORMAT_R16G16B16A16_SFLOAT);
            },
            [=](const PassData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd) {
                RaytracingExecutionContext ctx(reg.graph, reg.pass, cmd);
                
                RaytracingPipelineDescription desc;
                desc.raygen_shader = "raytracing/raytrace.rgen";
                desc.miss_shaders = { "raytracing/miss.rmiss" };
                desc.hit_shaders = { { "raytracing/closesthit.rchit", "", "" } };

                ctx.BindPipeline(desc);
                ctx.TraceRays(reg.graph.GetWidth(), reg.graph.GetHeight());
            }
        );
    }
}
