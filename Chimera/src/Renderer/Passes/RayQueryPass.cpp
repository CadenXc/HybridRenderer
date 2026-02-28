#include "pch.h"
#include "RayQueryPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"
#include "Scene/Scene.h"

namespace Chimera::RayQueryPass
{
    struct PassData { RGResourceHandle output, depth; };

    void AddToGraph(RenderGraph& graph, std::shared_ptr<Scene> scene)
    {
        if (!scene || scene->GetTLAS() == VK_NULL_HANDLE) return;

        graph.AddPass<PassData>("RayQueryPass",
            [&](PassData& data, RenderGraph::PassBuilder& builder)
            {
                data.output = builder.Write(RS::FinalColor).Format(VK_FORMAT_R16G16B16A16_SFLOAT);
                data.depth  = builder.Write(RS::Depth).Format(VK_FORMAT_D32_SFLOAT);
            },
            [scene](const PassData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd)
            {
                GraphicsExecutionContext ctx(reg.graph, reg.pass, cmd);
                
                GraphicsPipelineDescription desc{};
                desc.name = "RayQuery_Pipeline";
                desc.vertex_shader = "forward/forward.vert";
                desc.fragment_shader = "raytracing/rayquery.frag";
                
                ctx.DrawMeshes(desc, scene.get());
            }
        );
    }
}
