#include "pch.h"
#include "SVGFAtrousPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraphCommon.h"

namespace Chimera {

    void SVGFAtrousPass::Setup(RenderGraph& graph)
    {
        // 这一步逻辑较复杂，因为涉及多次迭代 (Ping-Pong)
        // 在新契约下，我们只需确保基础资源的正确
        graph.AddComputePass({
            .Name = "SVGFAtrousPass",
            .Dependencies = {
                TransientResource::Image(RS::Normal, VK_FORMAT_R16G16B16A16_SFLOAT),
                TransientResource::Image(RS::Depth, VK_FORMAT_D32_SFLOAT),
                TransientResource::Image(RS::SVGFOutput, VK_FORMAT_R16G16_SFLOAT)
            },
            .Outputs = {
                TransientResource::StorageImage(RS::AtrousPing, VK_FORMAT_R16G16_SFLOAT)
            },
            .Pipeline = {
                .kernels = { { "Atrous", "svgf_atrous.comp" } }
            },
            .Callback = [w = m_Width, h = m_Height](ComputeExecutionContext& ctx) {
                ctx.Dispatch(w, h, 1);
            },
            .ShaderLayout = "AtrousLayout"
        });
    }

}