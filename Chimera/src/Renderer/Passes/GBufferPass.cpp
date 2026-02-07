#include "pch.h"
#include "GBufferPass.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/Resources/Buffer.h"
#include "Renderer/Backend/ShaderResourceNames.h"
#include "Scene/Model.h"
#include <glm/gtc/matrix_inverse.hpp>

namespace Chimera {

    GBufferPass::GBufferPass(std::shared_ptr<Scene> scene)
        : RenderGraphPass("G-Buffer Pass"), m_Scene(scene)
    {
    }

    void GBufferPass::Setup(RenderGraph& graph)
    {
        auto scene = m_Scene;
        
        GraphicsPassSpecification spec;
        spec.Name = m_Name;
        spec.Dependencies = { 
            InputBuffer("MaterialBuffer", m_Scene->GetMaterialBuffer()),
            TransientResource::Sampler("TextureArray", 0, (uint32_t)ResourceManager::Get()->GetTextures().size()) 
        };
        spec.Outputs = { 
            OutputAttachment(RS::ALBEDO, VK_FORMAT_R8G8B8A8_UNORM),
            OutputAttachment(RS::NORMAL, VK_FORMAT_R16G16B16A16_SFLOAT),
            OutputAttachment(RS::MATERIAL, VK_FORMAT_R8G8B8A8_UNORM),
            OutputAttachment(RS::MOTION, VK_FORMAT_R16G16_SFLOAT),
            OutputAttachment(RS::DEPTH, VK_FORMAT_D32_SFLOAT) 
        };
        spec.Pipelines = { CreatePipelineDescription() };
        spec.ShaderLayout = "GBuffer_Standard";
        
        spec.Callback = [scene](ExecuteGraphicsCallback execute) {
            execute("G-Buffer Pipeline", [scene](GraphicsExecutionContext& ctx) {
                auto& entities = scene->GetEntities();
                for (auto& entity : entities) {
                    ctx.BindVertexBuffer(entity.mesh.model->GetVertexBuffer(), 0);
                    ctx.BindIndexBuffer(entity.mesh.model->GetIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
                    glm::mat4 entityWorldTransform = entity.transform.GetTransform();
                    for (auto& mesh : entity.mesh.model->GetMeshes()) {
                        GBufferPushConstants push = { entityWorldTransform * mesh.transform, (int)entity.mesh.material.Get().id };
                        ctx.PushConstants(push);
                        ctx.DrawIndexed(mesh.indexCount, 1, mesh.indexOffset, mesh.vertexOffset, 0);
                    }
                }
            });
        };

        graph.AddGraphicsPass(spec);
    }

    GraphicsPipelineDescription GBufferPass::CreatePipelineDescription()
    {
        GraphicsPipelineDescription pipelineDesc{};
        pipelineDesc.name = "G-Buffer Pipeline";
        pipelineDesc.vertex_shader = Shaders::GBufferV;
        pipelineDesc.fragment_shader = Shaders::GBufferF;
        pipelineDesc.push_constants = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(GBufferPushConstants) };
        return pipelineDesc;
    }

}
