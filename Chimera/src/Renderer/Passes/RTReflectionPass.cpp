#include "pch.h"
#include "RTReflectionPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/RaytracingExecutionContext.h"
#include "Scene/Scene.h"

namespace Chimera
{
    void RTReflectionPass::AddToGraph(RenderGraph& graph, std::shared_ptr<Scene> scene)
    {
        if (!scene)
        {
            return;
        }

        graph.AddPass<RTReflectionData>("RTReflectionPass",
            [](RTReflectionData& data, RenderGraph::PassBuilder& builder)
            {
                data.output = builder.WriteStorage("ReflectionRaw", VK_FORMAT_R16G16B16A16_SFLOAT);
                data.normal = builder.Read(RS::Normal);
                data.depth = builder.Read(RS::Depth);
                data.material = builder.Read(RS::Material);
                data.albedo = builder.Read(RS::Albedo);
            },
            [scene](const RTReflectionData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd)
            {
                RaytracingExecutionContext ctx(reg.graph, reg.pass, cmd);

                RaytracingPipelineDescription desc;
                desc.raygen_shader = "raytracing/reflection.rgen";
                desc.miss_shaders = { "raytracing/miss.rmiss" };
                desc.hit_shaders = { { "raytracing/closesthit.rchit", "", "" } };

                int skyboxIndex = scene ? scene->GetSkyboxTextureIndex() : -1;

                ctx.BindPipeline(desc);
                ctx.PushConstants(VK_SHADER_STAGE_ALL, skyboxIndex);
                ctx.TraceRays(reg.graph.GetWidth(), reg.graph.GetHeight());
            }
        );
    }
}
