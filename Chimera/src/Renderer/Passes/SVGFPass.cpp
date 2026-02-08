#include "pch.h"
#include "SVGFPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/ChimeraCommon.h"

namespace Chimera {

    void SVGFPass::Setup(RenderGraph& graph)
    {
        graph.AddComputePass({
            .Name = "SVGFPass",
            .Dependencies = {
                TransientResource::Image(RS::Normal, VK_FORMAT_R16G16B16A16_SFLOAT),
                TransientResource::Image(RS::Depth, VK_FORMAT_D32_SFLOAT),
                TransientResource::Image(RS::ShadowAO, VK_FORMAT_R16G16_SFLOAT)
            },
            .Outputs = { TransientResource::StorageImage(RS::SVGFOutput, VK_FORMAT_R16G16_SFLOAT) },
            .Pipeline = { .kernels = { { "SVGF", "svgf.comp" } } },
            .Callback = [w = m_Width, h = m_Height](ComputeExecutionContext& ctx) {
                ctx.Dispatch(w / 8, h / 8, 1);
            }
        });
    }

}