#include "pch.h"
#include "GBufferPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"
#include "Scene/Scene.h"

namespace Chimera
{
    void GBufferPass::AddToGraph(RenderGraph& graph, std::shared_ptr<Scene> scene)
    {
        graph.AddPass<GBufferData>("GBufferPass",
            [](GBufferData& data, RenderGraph::PassBuilder& builder)
            {
                data.albedo   = builder.Write(RS::Albedo,   VK_FORMAT_R8G8B8A8_UNORM);
                data.normal   = builder.Write(RS::Normal,   VK_FORMAT_R16G16B16A16_SFLOAT);
                data.material = builder.Write(RS::Material, VK_FORMAT_R8G8B8A8_UNORM);
                data.motion   = builder.Write(RS::Motion,   VK_FORMAT_R16G16_SFLOAT);
                data.depth    = builder.Write(RS::Depth,    VK_FORMAT_D32_SFLOAT);
            },
            [scene](const GBufferData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd)
            {
                GraphicsExecutionContext ctx(reg.graph, reg.pass, cmd);
                
                GraphicsPipelineDescription desc;
                desc.name = "GBuffer";
                desc.vertex_shader = "hybrid/gbuffer.vert";
                desc.fragment_shader = "hybrid/gbuffer.frag";
                desc.depth_test = true;
                desc.depth_write = true;

                ctx.DrawMeshes(desc, scene.get());
            }
        );
    }
}
