#include "pch.h"
#include "RayQueryPass.h"
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

        graph.AddPass<PassData>("RayQueryPass",
            [&](PassData& data, RenderGraph::PassBuilder& builder)
            {
                auto& frameCtx = Application::Get().GetFrameContext();
                
                VkClearColorValue clearColor = { {frameCtx.ClearColor.r, frameCtx.ClearColor.g, frameCtx.ClearColor.b, frameCtx.ClearColor.a} };
                VkClearColorValue clearMotion = { {0.0f, 0.0f, 0.0f, 0.0f} };

                data.output = builder.Write(RS::FinalColor)
                                     .Format(VK_FORMAT_R16G16B16A16_SFLOAT)
                                     .Clear(clearColor);
                
                data.motion = builder.Write(RS::Motion)
                                     .Format(VK_FORMAT_R16G16_SFLOAT)
                                     .Clear(clearMotion);

                data.depth  = builder.Write(RS::Depth)
                                     .Format(VK_FORMAT_D32_SFLOAT)
                                     .ClearDepthStencil(1.0f, 0);
            },
            [scene](const PassData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd)
            {
                GraphicsExecutionContext ctx(reg.graph, reg.pass, cmd);
                
                GraphicsPipelineDescription desc{};
                desc.name = "RayQuery_Pipeline";
                desc.vertex_shader = "Forward_Vert";
                desc.fragment_shader = "RayQuery_Frag";
                
                ctx.DrawMeshes(desc, scene.get());
            }
        );
    }
}
