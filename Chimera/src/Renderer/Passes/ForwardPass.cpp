#include "pch.h"
#include "ForwardPass.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"
#include "Renderer/Graph/ResourceNames.h"

namespace Chimera {

    struct ForwardPushConstants {
        glm::mat4 model;
        glm::mat4 normalMatrix;
    };

    ForwardPass::ForwardPass(std::shared_ptr<Scene> scene)
        : RenderGraphPass("Forward Pass"), m_Scene(scene)
    {
    }

    void ForwardPass::Setup(RenderGraph& graph)
    {
        // Use ForwardColor
        auto renderOutput = TransientResource::Image(RS::FORWARD_COLOR, VK_FORMAT_B8G8R8A8_UNORM, 0, { {0.1f, 0.1f, 0.1f, 1.0f} });
        auto depth        = TransientResource::Image(RS::DEPTH, VK_FORMAT_D32_SFLOAT, 1, { 1.0f, 0 });

        GraphicsPipelineDescription pipelineDesc{};
        pipelineDesc.name = "Forward Pipeline";
        pipelineDesc.vertex_shader = "shader.vert";
        pipelineDesc.fragment_shader = "shader.frag";
        pipelineDesc.vertex_input_state = VertexInputState::Default;
        
        pipelineDesc.Rasterization.Cull = CullMode::None;
        pipelineDesc.DepthStencil.DepthTest = true;
        pipelineDesc.DepthStencil.DepthWrite = true;
        
        pipelineDesc.dynamic_state = DynamicState::ViewportScissor;
        pipelineDesc.push_constants.shader_stage = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pipelineDesc.push_constants.size = sizeof(ForwardPushConstants);

        auto scene = m_Scene;
        graph.AddGraphicsPass(m_Name,
            {}, 
            { renderOutput, depth }, 
            { pipelineDesc },
            [scene](ExecuteGraphicsCallback execute_pipeline) {
                execute_pipeline("Forward Pipeline", [scene](GraphicsExecutionContext& ctx) {
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
                            ForwardPushConstants push{};
                            glm::mat4 worldTransform = instance.transform * mesh.transform;
                            push.model = worldTransform;
                            push.normalMatrix = glm::transpose(glm::inverse(worldTransform));
                            
                            ctx.PushConstants(push);
                            ctx.DrawIndexed(mesh.indexCount, 1, mesh.indexOffset, mesh.vertexOffset, 0);
                        }
                    }
                });
            }
        );
    }

}