#include "pch.h"
#include "ForwardPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Scene/Scene.h"

namespace Chimera {

    void ForwardPass::Setup(RenderGraph& graph)
    {
        graph.AddGraphicsPass({
            .Name = "ForwardPass",
            .Outputs = {
                TransientResource::Attachment(RS::FinalColor, VK_FORMAT_B8G8R8A8_UNORM, {0.1f, 0.2f, 0.4f, 1.0f}),
                TransientResource::Attachment(RS::Depth, VK_FORMAT_D32_SFLOAT, {1.0f, 0})
            },
            .Pipelines = { { "Forward", "shader.vert", "shader.frag" } },
            .Callback = [scene = m_Scene](ExecuteGraphicsCallback& execute) {
                execute("Forward", [scene](GraphicsExecutionContext& ctx) {
                    scene->RenderMeshes(ctx);
                });
            }
        });
    }

}
