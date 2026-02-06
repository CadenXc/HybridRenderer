#include "pch.h"
#include "GBufferPass.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"
#include "Renderer/Graph/ResourceNames.h"

namespace Chimera {

    struct GBufferPushConstants
    {
        glm::mat4 model;
        glm::mat4 normalMatrix;
        int materialIndex;
    };

    GBufferPass::GBufferPass(std::shared_ptr<Scene> scene)
        : RenderGraphPass("G-Buffer Pass"), m_Scene(scene)
    {
    }

    void GBufferPass::Setup(RenderGraph& graph)
    {
        // 1. Define resources - Use Constants
        auto renderOutput = TransientResource::Image(RS::ALBEDO, VK_FORMAT_B8G8R8A8_UNORM, 0, { { 0.1f, 0.1f, 0.1f, 1.0f } });
        auto normalOut    = TransientResource::Image(RS::NORMAL, VK_FORMAT_R16G16B16A16_SFLOAT, 1);
        auto materialOut  = TransientResource::Image(RS::MATERIAL, VK_FORMAT_R8G8B8A8_UNORM, 2);
        auto motionOut    = TransientResource::Image(RS::MOTION, VK_FORMAT_R16G16B16A16_SFLOAT, 3);
        auto depthOut     = TransientResource::Image(RS::DEPTH, VK_FORMAT_D32_SFLOAT, 4, { 1.0f, 0 });

        // 2. Pipeline Description
        GraphicsPipelineDescription pipelineDesc{};
        pipelineDesc.name = "G-Buffer Pipeline";
        pipelineDesc.vertex_shader = "gbuffer.vert"; 
        pipelineDesc.fragment_shader = "gbuffer.frag";
        pipelineDesc.push_constants.shader_stage = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pipelineDesc.push_constants.size = sizeof(GBufferPushConstants);
        
        pipelineDesc.Rasterization.Cull = CullMode::Back;
        pipelineDesc.Rasterization.Front = FrontFace::CounterClockwise;
        
        pipelineDesc.DepthStencil.DepthTest = true;
        pipelineDesc.DepthStencil.DepthWrite = true;
        
        pipelineDesc.dynamic_state = DynamicState::ViewportScissor;
        pipelineDesc.vertex_input_state = VertexInputState::Default;

        // 3. Add to Graph
        auto scene = m_Scene; 
        uint32_t texCount = (uint32_t)ResourceManager::Get()->GetTextures().size();

        graph.AddGraphicsPass(m_Name,
            { 
                TransientResource::Buffer("MaterialBuffer", 0, m_Scene->GetMaterialBuffer()), 
                TransientResource::Sampler("TextureArray", 1, std::max(1u, texCount)) 
            }, 
            { renderOutput, normalOut, materialOut, motionOut, depthOut }, 
            { pipelineDesc }, 
            [scene](ExecuteGraphicsCallback execute) 
            {
                execute("G-Buffer Pipeline",
                    [scene](GraphicsExecutionContext& ctx) 
                    {
                        if (!scene) return;

                        auto size = ctx.GetDisplaySize();
                        // Negative viewport height to flip Y coordinate correctly in Vulkan
                        VkViewport viewport{ 0.0f, (float)size.y, (float)size.x, -(float)size.y, 0.0f, 1.0f };
                        VkRect2D scissor{ {0, 0}, {size.x, size.y} };
                        ctx.SetViewport(viewport);
                        ctx.SetScissor(scissor);

                        const auto& instances = scene->GetInstances();
                        for (const auto& instance : instances)
                        {
                            auto model = instance.model;
                            if (!model) continue;

                            ctx.BindVertexBuffer(model->GetVertexBuffer(), 0);
                            ctx.BindIndexBuffer(model->GetIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

                            const auto& meshes = model->GetMeshes();
                            for (const auto& mesh : meshes)
                            {
                                GBufferPushConstants pc{};
                                // Combine instance transform and mesh relative transform
                                glm::mat4 worldTransform = instance.transform * mesh.transform;
                                pc.model = worldTransform;
                                pc.normalMatrix = glm::transpose(glm::inverse(worldTransform));
                                pc.materialIndex = mesh.materialIndex;
                                
                                ctx.PushConstants(pc);
                                ctx.DrawIndexed(mesh.indexCount, 1, mesh.indexOffset, mesh.vertexOffset, 0);
                            }
                        }
                    }
                );
            }
        );
    }

}