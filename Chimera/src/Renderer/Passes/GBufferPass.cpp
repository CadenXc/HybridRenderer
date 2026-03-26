#include "pch.h"
#include "GBufferPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"
#include "Scene/Scene.h"
#include "Scene/Model.h"
#include "Core/Application.h"

namespace Chimera
{
    GBufferPass::GBufferPass(std::shared_ptr<Scene> scene)
        : m_Scene(scene)
    {
    }

    void GBufferPass::Setup(PassData& data, RenderGraph::PassBuilder& builder)
    {
        auto &frameCtx = Application::Get().GetFrameContext();

        VkClearColorValue clearColor = {{frameCtx.ClearColor.r, frameCtx.ClearColor.g, frameCtx.ClearColor.b, frameCtx.ClearColor.a}};
        VkClearColorValue clearZero = {{0.0f, 0.0f, 0.0f, 0.0f}};
        VkClearColorValue clearNormal = {{0.0f, 0.0f, 1.0f, 0.0f}};

        data.albedo = builder.Write(RS::Albedo)
                          .Format(VK_FORMAT_R8G8B8A8_UNORM)
                          .Clear(clearColor);

        data.normal = builder.Write(RS::Normal)
                          .Format(VK_FORMAT_R16G16B16A16_SFLOAT)
                          .Clear(clearNormal)
                          .SaveAsHistory(RS::Normal);

        data.material = builder.Write(RS::Material)
                            .Format(VK_FORMAT_R8G8B8A8_UNORM)
                            .Clear(clearZero);

        data.motion = builder.Write(RS::Motion)
                          .Format(VK_FORMAT_R16G16_SFLOAT)
                          .Clear(clearZero);

        data.emissive = builder.Write(RS::Emissive)
                            .Format(VK_FORMAT_R16G16B16A16_SFLOAT)
                            .Clear(clearZero);

        // Write to Depth, establishing dependency on Prepass which already wrote to it.
        data.depth = builder.Write(RS::Depth)
                         .Format(VK_FORMAT_D32_SFLOAT)
                         .SaveAsHistory(RS::Depth); // Reuse from Prepass
    }

    void GBufferPass::Execute(const PassData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd)
    {
        if (!m_Scene)
            return;

        GraphicsExecutionContext ctx(reg.graph, reg.pass, cmd);

        GraphicsPipelineDescription desc;
        desc.name = "GBuffer";
        desc.vertex_shader = "GBuffer_Vert";
        desc.fragment_shader = "GBuffer_Frag";
        desc.depth_test = true;
        desc.depth_write = true;
        desc.depth_compare_op = CH_DEPTH_COMPARE_OP;
        desc.cull_mode = VK_CULL_MODE_NONE;

        ctx.BindPipeline(desc);

        const auto& frustum = Application::Get().GetFrameContext().CamFrustum;

        // Use Octree to get visible entities
        std::vector<uint32_t> visibleEntityIndices;
        m_Scene->GetVisibleEntities(frustum, visibleEntityIndices);

        const auto& allEntities = m_Scene->GetEntities();
        uint32_t totalMeshes = 0;
        for (const auto& e : allEntities) if (e.mesh.model) totalMeshes += (uint32_t)e.mesh.model->GetMeshes().size();

        uint32_t drawCount = 0;

        for (uint32_t entityIdx : visibleEntityIndices)
        {
            const auto& entity = allEntities[entityIdx];
            if (entity.mesh.model)
            {
                const auto& meshes = entity.mesh.model->GetMeshes();

                VkBuffer vBuffer = (VkBuffer)entity.mesh.model->GetVertexBuffer()->GetBuffer();
                VkDeviceSize offset = 0;
                ctx.BindVertexBuffers(0, 1, &vBuffer, &offset);
                ctx.BindIndexBuffer((VkBuffer)entity.mesh.model->GetIndexBuffer()->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

                glm::mat4 entityTransform = entity.transform.GetTransform();

                uint32_t meshOffset = 0;
                for (const auto& mesh : meshes)
                {
                    // Secondary culling at mesh level
                    AABB worldBounds = mesh.localBounds.Transform(entityTransform * mesh.transform);
                    if (!frustum.Intersects(worldBounds))
                    {
                        meshOffset++;
                        continue;
                    }

                    ScenePushConstants pc{ entity.primitiveOffset + meshOffset };
                    ctx.PushConstants(VK_SHADER_STAGE_ALL, pc);
                    ctx.DrawIndexed(mesh.indexCount, 1, mesh.indexOffset, (int32_t)mesh.vertexOffset, 0);
                    drawCount++;
                    meshOffset++;
                }
            }
        }

        uint32_t culledCount = totalMeshes - drawCount;

        // Update Application Stats for UI
        FrameStats stats;
        stats.DrawCalls = drawCount;
        stats.TotalMeshes = totalMeshes;
        stats.CulledMeshes = culledCount;
        Application::Get().SetFrameStats(stats);
    }
}
