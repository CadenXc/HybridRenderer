#include "pch.h"
#include "ForwardPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"
#include "Scene/Scene.h"
#include "Scene/Model.h" 
#include "Core/Application.h"

namespace Chimera::ForwardPass
{
    struct PassData { RGResourceHandle output, depth; };

    void AddToGraph(RenderGraph& graph, std::shared_ptr<Scene> scene)
    {
        graph.AddPass<PassData>("ForwardPass",
            [&](PassData& data, RenderGraph::PassBuilder& builder)
            {
                auto& frameCtx = Application::Get().GetFrameContext();
                VkClearColorValue clearVal;
                clearVal.float32[0] = frameCtx.ClearColor.r;
                clearVal.float32[1] = frameCtx.ClearColor.g;
                clearVal.float32[2] = frameCtx.ClearColor.b;
                clearVal.float32[3] = frameCtx.ClearColor.a;

                data.output = builder.Write(RS::FinalColor).Format(VK_FORMAT_R16G16B16A16_SFLOAT).Clear(clearVal);
                data.depth  = builder.Write(RS::Depth).Format(VK_FORMAT_D32_SFLOAT).ClearDepthStencil(0.0f);
            },
            [scene](const PassData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd)
            {
                GraphicsExecutionContext ctx(reg.graph, reg.pass, cmd);
                
                GraphicsPipelineDescription desc{ "Forward", "forward/forward.vert", "forward/forward.frag", true, true };
                ctx.BindPipeline(desc);

                if (scene)
                {
                    const auto& entities = scene->GetEntities();
                    uint32_t globalObjectId = 0; 

                    for (const auto& entity : entities)
                    {
                        if (entity.mesh.model)
                        {
                            const auto& meshes = entity.mesh.model->GetMeshes();
                            
                            VkBuffer vBuffer = (VkBuffer)entity.mesh.model->GetVertexBuffer()->GetBuffer();
                            VkDeviceSize offset = 0;
                            ctx.BindVertexBuffers(0, 1, &vBuffer, &offset);
                            ctx.BindIndexBuffer((VkBuffer)entity.mesh.model->GetIndexBuffer()->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

                            for (const auto& mesh : meshes)
                            {
                                ScenePushConstants pc{ globalObjectId++ };
                                ctx.PushConstants(VK_SHADER_STAGE_ALL, pc);
                                ctx.DrawIndexed(mesh.indexCount, 1, mesh.indexOffset, (int32_t)mesh.vertexOffset, 0);
                            }
                        }
                    }
                }
            }
        );
    }
}
