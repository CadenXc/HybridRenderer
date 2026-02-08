#include "pch.h"
#include "GBufferPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/ChimeraCommon.h"
#include "Scene/Scene.h"

namespace Chimera {

    void GBufferPass::Setup(RenderGraph& graph)
    {
        graph.AddGraphicsPass({
            .Name = "GBufferPass",
            .Outputs = {
                TransientResource::Attachment(RS::Albedo, VK_FORMAT_R8G8B8A8_UNORM),
                TransientResource::Attachment(RS::Normal, VK_FORMAT_R16G16B16A16_SFLOAT),
                TransientResource::Attachment(RS::Material, VK_FORMAT_R8G8B8A8_UNORM),
                TransientResource::Attachment(RS::Motion, VK_FORMAT_R16G16B16A16_SFLOAT),
                TransientResource::Attachment(RS::Depth, VK_FORMAT_D32_SFLOAT, {1.0f, 0})
            },
            .Pipelines = { { "GBuffer", "gbuffer.vert", "gbuffer.frag" } },
            .Callback = [scene = m_Scene](ExecuteGraphicsCallback& execute) {
                execute("GBuffer", [scene](GraphicsExecutionContext& ctx) {
                    scene->RenderMeshes(ctx);
                });
            }
        });
    }

}
