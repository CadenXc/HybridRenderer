#include "pch.h"
#include "ForwardPass.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Backend/ShaderResourceNames.h"

namespace Chimera {

    ForwardPass::ForwardPass(std::shared_ptr<Scene> scene)
        : RenderGraphPass("Forward Pass"), m_Scene(scene)
    {
    }

    void ForwardPass::Setup(RenderGraph& graph)
    {
        auto scene = m_Scene;

        GraphicsPassSpecification spec;
        spec.Name = m_Name;
        spec.Outputs = { 
            TransientResource::Image(RS::FINAL_COLOR, VK_FORMAT_B8G8R8A8_UNORM, 0xFFFFFFFF, { {0.1f, 0.1f, 0.1f, 1.0f} }),
            TransientResource::Image(RS::DEPTH, VK_FORMAT_D32_SFLOAT, 0xFFFFFFFF, { 1.0f, 0 })
        };
        spec.Pipelines = { CreatePipelineDescription() };
        
        spec.Callback = [scene](ExecuteGraphicsCallback execute_pipeline) {
            execute_pipeline("Forward Pipeline", [scene](GraphicsExecutionContext& ctx) {
                if (!scene) return;

                auto size = ctx.GetDisplaySize();
                VkViewport viewport{ 0.0f, (float)size.y, (float)size.x, -(float)size.y, 0.0f, 1.0f };
                VkRect2D scissor{ {0, 0}, {size.x, size.y} };
                ctx.SetViewport(viewport);
                ctx.SetScissor(scissor);

                const auto& entities = scene->GetEntities();
                for (const auto& entity : entities)
                {
                    auto model = entity.mesh.model;
                    if (!model) continue;

                    ctx.BindVertexBuffer(model->GetVertexBuffer(), 0);
                    ctx.BindIndexBuffer(model->GetIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

                    const auto& meshes = model->GetMeshes();
                    for (const auto& mesh : meshes)
                    {
                        ForwardPushConstants push{};
                        glm::mat4 entityWorldTransform = entity.transform.GetTransform();
                        glm::mat4 worldTransform = entityWorldTransform * mesh.transform;
                        push.model = worldTransform;
                        push.normalMatrix = glm::transpose(glm::inverse(worldTransform));
                        push.materialIndex = (int)entity.mesh.material.Get().id;
                        
                        ctx.PushConstants(push);
                        ctx.DrawIndexed(mesh.indexCount, 1, mesh.indexOffset, mesh.vertexOffset, 0);
                    }
                }
            });
        };

        graph.AddGraphicsPass(spec);
    }

    GraphicsPipelineDescription ForwardPass::CreatePipelineDescription()
    {
        GraphicsPipelineDescription pipelineDesc{};
        pipelineDesc.name = "Forward Pipeline";
        pipelineDesc.vertex_shader = Shaders::ForwardV;
        pipelineDesc.fragment_shader = Shaders::ForwardF;
        pipelineDesc.vertex_input_state = VertexInputState::Default;
        
        pipelineDesc.Rasterization.Cull = CullMode::None;
        pipelineDesc.DepthStencil.DepthTest = true;
        pipelineDesc.DepthStencil.DepthWrite = true;
        
        pipelineDesc.dynamic_state = DynamicState::ViewportScissor;
        pipelineDesc.push_constants.shader_stage = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pipelineDesc.push_constants.size = sizeof(ForwardPushConstants);
        return pipelineDesc;
    }

}
