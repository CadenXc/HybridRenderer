#include "pch.h"
#include "RTDiffuseGIPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/RaytracingExecutionContext.h"
#include "Scene/Scene.h"

namespace Chimera::RTDiffuseGIPass
{
    struct PassData
    {
        RGResourceHandle normal;
        RGResourceHandle depth;
        RGResourceHandle material;
        RGResourceHandle output;
    };

    void AddToGraph(RenderGraph& graph, std::shared_ptr<Scene> scene)
    {
        if (!scene) return;

        graph.AddPass<PassData>("RTDiffuseGIPass",
            [](PassData& data, RenderGraph::PassBuilder& builder) 
            {
                data.output   = builder.WriteStorage("GIRaw").Format(VK_FORMAT_R16G16B16A16_SFLOAT);
                data.normal   = builder.Read(RS::Normal);
                data.depth    = builder.Read(RS::Depth);
                data.material = builder.Read(RS::Material);
            },
            [scene](const PassData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd) 
            {
                RaytracingExecutionContext ctx(reg.graph, reg.pass, cmd);
                
                RaytracingPipelineDescription desc;
                desc.raygen_shader = "raytracing/diffuse_gi.rgen";
                desc.miss_shaders = { "raytracing/miss.rmiss" };
                desc.hit_shaders = { { "raytracing/closesthit.rchit", "", "" } };

                int skyboxIndex = scene->GetSkyboxTextureIndex();
                
                ctx.BindPipeline(desc);
                ctx.PushConstants(VK_SHADER_STAGE_ALL, skyboxIndex);
                ctx.TraceRays(reg.graph.GetWidth(), reg.graph.GetHeight());
            }
        );
    }
}
