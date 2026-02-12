#include "pch.h"
#include "RTShadowAOPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"

namespace Chimera
{
    void RTShadowAOPass::AddToGraph(RenderGraph& graph, std::shared_ptr<Scene> scene)
    {
        graph.AddPass<RTShadowAOData>("RTShadowAOPass",
            [](RTShadowAOData& data, RenderGraph::PassBuilder& builder) 
            {
                data.output = builder.WriteStorage(RS::ShadowAO, VK_FORMAT_R16G16B16A16_SFLOAT);
                data.normal = builder.Read(RS::Normal);
                data.depth  = builder.Read(RS::Depth);
            },
            [](const RTShadowAOData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd) 
            {
                GraphicsExecutionContext ctx(reg.graph, reg.pass, cmd);
                
                RaytracingPipelineDescription desc;
                // [FIX] 使用完整子目录路径
                desc.raygen_shader = "raytracing/raygen.rgen";
                desc.miss_shaders = { "raytracing/miss.rmiss", "raytracing/shadow.rmiss" };
                desc.hit_shaders = { { "raytracing/closesthit.rchit", "", "" } };

                ctx.DispatchRays(desc);
            }
        );
    }
}