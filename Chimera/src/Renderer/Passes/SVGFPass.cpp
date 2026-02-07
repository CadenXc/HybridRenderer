#include "pch.h"
#include "SVGFPass.h"
#include "Renderer/Graph/ComputeExecutionContext.h"
#include "Renderer/Graph/ResourceNames.h"

namespace Chimera {

    SVGFPass::SVGFPass() : RenderGraphPass("SVGF Pass") {}

    void SVGFPass::Setup(RenderGraph& graph)
    {
        ComputePipelineDescription pipelineDesc{};
        pipelineDesc.kernels = { { "svgf.comp" } };
        pipelineDesc.push_constant_description.size = 0;
        pipelineDesc.push_constant_description.shader_stage = VK_SHADER_STAGE_COMPUTE_BIT;

        graph.AddComputePass(m_Name,
            {
                TransientResource::Image(RS::NORMAL, VK_FORMAT_R16G16B16A16_SFLOAT, 0, {}, TransientImageType::StorageImage),
                TransientResource::Image(RS::MOTION, VK_FORMAT_R16G16_SFLOAT, 1, {}, TransientImageType::StorageImage),
                TransientResource::Image(RS::DEPTH, VK_FORMAT_D32_SFLOAT, 2),
                TransientResource::Image(RS::RT_SHADOW_AO, VK_FORMAT_R16G16_SFLOAT, 3, {}, TransientImageType::StorageImage),
                TransientResource::Image(RS::PREV_NORMAL, VK_FORMAT_R16G16B16A16_SFLOAT, 5, {}, TransientImageType::StorageImage),
                TransientResource::Image(RS::PREV_DEPTH, VK_FORMAT_D32_SFLOAT, 6),
                TransientResource::Image(RS::SHADOW_AO_HIST, VK_FORMAT_R16G16B16A16_SFLOAT, 7, {}, TransientImageType::StorageImage),
                TransientResource::Image(RS::MOMENTS_HIST, VK_FORMAT_R16G16_SFLOAT, 8, {}, TransientImageType::StorageImage)
            },
            {
                TransientResource::Image(RS::SVGF_OUTPUT, VK_FORMAT_R16G16B16A16_SFLOAT, 4, {}, TransientImageType::StorageImage)
            },
            pipelineDesc,
            [](ComputeExecutionContext& ctx) {
                auto size = ctx.GetDisplaySize();
                ctx.Dispatch("svgf.comp", size.x / 8 + (size.x % 8 != 0), size.y / 8 + (size.y % 8 != 0), 1);
            },
            "SVGF_Standard"
        );
    }

}
