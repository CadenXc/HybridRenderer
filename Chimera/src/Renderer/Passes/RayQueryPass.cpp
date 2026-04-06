#include "pch.h"
#include "RayQueryPass.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"
#include "Scene/Scene.h"
#include "Core/Application.h"

namespace Chimera::RayQueryPass
{
struct PassData
{
    RGResourceHandle output;
    RGResourceHandle motion;
    RGResourceHandle depth;
};

void AddToGraph(RenderGraph& graph, std::shared_ptr<Scene> scene)
{
    if (!scene || scene->GetTLAS() == VK_NULL_HANDLE)
    {
        return;
    }

    graph.AddPassRaw<PassData>(
        "RayQueryPass",
        [&](PassData& data, RenderGraph::PassBuilder& builder)
        {
            data.output = builder.Write(RS::FinalColor)
                              .Format(VK_FORMAT_R16G16B16A16_SFLOAT);
            data.motion = builder.Write(RS::Motion)
                              .Format(VK_FORMAT_R16G16_SFLOAT)
                              .Clear({0.0f, 0.0f, 0.0f, 0.0f});
            data.depth = builder.Write(RS::Depth)
                             .Format(VK_FORMAT_D32_SFLOAT)
                             .ClearDepthStencil(CH_DEPTH_CLEAR_VALUE);
        },
        [scene](const PassData& data, RenderGraphRegistry& reg,
                VkCommandBuffer cmd)
        {
            GraphicsExecutionContext ctx(reg.graph, reg.pass, cmd);

            GraphicsPipelineDescription desc{};
            desc.name = "RayQuery_Pipeline";
            desc.vertex_shader = "Forward_Vert";
            desc.fragment_shader = "RayQuery_Frag";

            ctx.DrawMeshes(desc, scene.get());
        });
}
} // namespace Chimera::RayQueryPass
