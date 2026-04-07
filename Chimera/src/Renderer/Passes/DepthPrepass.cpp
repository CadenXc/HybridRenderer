#include "pch.h"
#include "DepthPrepass.h"
#include "Renderer/ChimeraCommon.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"
#include "Renderer/Backend/ShaderCommon.h"
#include "Scene/Scene.h"
#include "Scene/Model.h"
#include "Core/Application.h"

namespace Chimera
{
DepthPrepass::DepthPrepass(std::shared_ptr<Scene> scene) : m_Scene(scene) {}

void DepthPrepass::Setup(PassData& data, RenderGraph::PassBuilder& builder)
{
    data.depth = builder.Write(RS::Depth)
                     .Format(VK_FORMAT_D32_SFLOAT)
                     .ClearDepthStencil(CH_DEPTH_CLEAR_VALUE);
}

void DepthPrepass::Execute(const PassData& data, RenderGraphRegistry& reg,
                           VkCommandBuffer cmd)
{
    if (!m_Scene) return;

    GraphicsExecutionContext ctx(reg.graph, reg.pass, cmd);

    GraphicsPipelineDescription desc;
    desc.name = "DepthPrepass";
    desc.vertex_shader = "GBuffer_Vert";
    desc.fragment_shader = ""; // No fragment shader for depth-only
    desc.depth_test = true;
    desc.depth_write = true;
    desc.depth_compare_op = CH_DEPTH_COMPARE_OP;
    desc.cull_mode = VK_CULL_MODE_NONE;
    ctx.BindPipeline(desc);

    const auto& entities = m_Scene->GetEntities();
    const auto& frustum = Application::Get().GetFrameContext().CamFrustum;
    uint32_t globalObjectId = 0;

    for (const auto& entity : entities)
    {
        if (entity.mesh.model)
        {
            const auto& meshes = entity.mesh.model->GetMeshes();

            VkBuffer vBuffer =
                (VkBuffer)entity.mesh.model->GetVertexBuffer()->GetBuffer();
            VkDeviceSize offset = 0;
            ctx.BindVertexBuffers(0, 1, &vBuffer, &offset);
            ctx.BindIndexBuffer(
                (VkBuffer)entity.mesh.model->GetIndexBuffer()->GetBuffer(), 0,
                VK_INDEX_TYPE_UINT32);

            glm::mat4 entityTransform = entity.transform.GetTransform();

            for (const auto& mesh : meshes)
            {
                ChimeraAABB worldBounds = mesh.localBounds.Transform(entityTransform *
                                                              mesh.transform);
                if (!frustum.Intersects(worldBounds))
                {
                    globalObjectId++;
                    continue;
                }

                ScenePushConstants pc{globalObjectId++};
                ctx.PushConstants(VK_SHADER_STAGE_ALL, pc);
                ctx.DrawIndexed(mesh.indexCount, 1, mesh.indexOffset,
                                (int32_t)mesh.vertexOffset, 0);
            }
        }
    }
}
} // namespace Chimera
