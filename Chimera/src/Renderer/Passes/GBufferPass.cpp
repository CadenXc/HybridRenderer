#include "pch.h"
#include "GBufferPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"
#include "Scene/Scene.h"
#include "Scene/Model.h"
#include "Core/Application.h"

namespace Chimera::GBufferPass
{
    // [ENCA] Internal data moved from header to source file
    struct GBufferData
    {
        RGResourceHandle albedo;
        RGResourceHandle normal;
        RGResourceHandle material;
        RGResourceHandle motion;
        RGResourceHandle emissive; // [NEW] HDR Emissive storage
        RGResourceHandle depth;
    };

    void AddToGraph(RenderGraph& graph, std::shared_ptr<Scene> scene)
    {
        if (!scene)
        {
            return;
        }

        graph.AddPass<GBufferData>("GBufferPass",
            [](GBufferData& data, RenderGraph::PassBuilder& builder)
            {
                auto& frameCtx = Application::Get().GetFrameContext();
                
                VkClearColorValue clearColor = { {frameCtx.ClearColor.r, frameCtx.ClearColor.g, frameCtx.ClearColor.b, frameCtx.ClearColor.a} };
                VkClearColorValue clearZero = { {0.0f, 0.0f, 0.0f, 0.0f} };
                VkClearColorValue clearNormal = { {0.0f, 0.0f, 1.0f, 0.0f} }; // Facing camera by default

                data.albedo   = builder.Write(RS::Albedo)
                                     .Format(VK_FORMAT_R8G8B8A8_UNORM)
                                     .Clear(clearColor);

                data.normal   = builder.Write(RS::Normal)
                                     .Format(VK_FORMAT_R16G16B16A16_SFLOAT)
                                     .Clear(clearNormal)
                                     .SaveAsHistory(RS::Normal);

                data.material = builder.Write(RS::Material)
                                     .Format(VK_FORMAT_R8G8B8A8_UNORM)
                                     .Clear(clearZero);

                data.motion   = builder.Write(RS::Motion)
                                     .Format(VK_FORMAT_R16G16_SFLOAT)
                                     .Clear(clearZero);
                
                data.emissive = builder.Write(RS::Emissive)
                                     .Format(VK_FORMAT_R16G16B16A16_SFLOAT)
                                     .Clear(clearZero);

                data.depth    = builder.Write(RS::Depth)
                                     .Format(VK_FORMAT_D32_SFLOAT)
                                     .ClearDepthStencil(CH_DEPTH_CLEAR_VALUE)
                                     .SaveAsHistory(RS::Depth);
            },
            [scene](const GBufferData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd)
            {
                GraphicsExecutionContext ctx(reg.graph, reg.pass, cmd);
                
                GraphicsPipelineDescription desc;
                desc.name = "GBuffer";
                desc.vertex_shader = "GBuffer_Vert";
                desc.fragment_shader = "GBuffer_Frag";
                desc.depth_test = true;
                desc.depth_write = true;

                ctx.BindPipeline(desc);

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
        );
    }
}
