#include "pch.h"
#include "RTShadowAOPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/RaytracingExecutionContext.h"

namespace Chimera::RTShadowAOPass
{
    struct PassData
    {
        RGResourceHandle normal;
        RGResourceHandle depth;
        RGResourceHandle output;
    };

    void AddToGraph(RenderGraph& graph, std::shared_ptr<Scene> scene)
    {
        graph.AddPass<PassData>("RTShadowAOPass",
            [](PassData& data, RenderGraph::PassBuilder& builder)
            {
                data.output = builder.WriteStorage("CurColor").Format(VK_FORMAT_R16G16B16A16_SFLOAT);
                data.normal = builder.Read(RS::Normal);
                data.depth  = builder.Read(RS::Depth);
            },
            [](const PassData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd) 
            {
                RaytracingExecutionContext ctx(reg.graph, reg.pass, cmd);
                
                RaytracingPipelineDescription desc;
                desc.raygen_shader = "raytracing/raygen.rgen";
                desc.miss_shaders = { "raytracing/miss.rmiss", "raytracing/shadow.rmiss" };
                desc.hit_shaders = { { "raytracing/closesthit.rchit", "", "" } };

                ctx.BindPipeline(desc);
                ctx.TraceRays(reg.graph.GetWidth(), reg.graph.GetHeight());
            }
        );
    }
}
