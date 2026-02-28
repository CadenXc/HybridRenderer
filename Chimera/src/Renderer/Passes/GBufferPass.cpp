#include "pch.h"
#include "GBufferPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"
#include "Scene/Scene.h"
#include "Scene/Model.h"
#include "Core/Application.h" // [FIX]

namespace Chimera
{
    void GBufferPass::AddToGraph(RenderGraph& graph, std::shared_ptr<Scene> scene)
    {
        graph.AddPass<GBufferData>("GBufferPass",
            [](GBufferData& data, RenderGraph::PassBuilder& builder)
            {
                auto& frameCtx = Application::Get().GetFrameContext();
                VkClearColorValue clearVal;
                clearVal.float32[0] = frameCtx.ClearColor.r;
                clearVal.float32[1] = frameCtx.ClearColor.g;
                clearVal.float32[2] = frameCtx.ClearColor.b;
                clearVal.float32[3] = frameCtx.ClearColor.a;

                data.albedo   = builder.Write(RS::Albedo).Format(VK_FORMAT_R8G8B8A8_UNORM).Clear(clearVal);
                data.normal   = builder.Write(RS::Normal).Format(VK_FORMAT_R16G16B16A16_SFLOAT).SaveAsHistory(RS::Normal);
                data.material = builder.Write(RS::Material).Format(VK_FORMAT_R8G8B8A8_UNORM);
                data.motion   = builder.Write(RS::Motion).Format(VK_FORMAT_R16G16_SFLOAT);
                data.depth    = builder.Write(RS::Depth).Format(VK_FORMAT_D32_SFLOAT).ClearDepthStencil(0.0f).SaveAsHistory(RS::Depth);
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
                                
                                // [FIX] vertexOffset added
                                ctx.DrawIndexed(mesh.indexCount, 1, mesh.indexOffset, (int32_t)mesh.vertexOffset, 0);
                            }
                        }
                    }
                }
            }
        );
    }
}
