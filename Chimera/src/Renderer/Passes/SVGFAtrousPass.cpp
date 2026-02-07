#include "pch.h"
#include "SVGFAtrousPass.h"
#include "Renderer/Graph/ComputeExecutionContext.h"
#include "Renderer/Graph/ResourceNames.h"

namespace Chimera {

    SVGFAtrousPass::SVGFAtrousPass(const std::string& name, const std::string& inputName, const std::string& outputName, int stepSize)
        : RenderGraphPass(name), m_InputName(inputName), m_OutputName(outputName), m_StepSize(stepSize)
    {
    }

    void SVGFAtrousPass::Setup(RenderGraph& graph)
    {
        ComputePipelineDescription pipelineDesc{};
        pipelineDesc.kernels = { { "svgf_atrous.comp" } };
        pipelineDesc.push_constant_description.size = sizeof(int);
        pipelineDesc.push_constant_description.shader_stage = VK_SHADER_STAGE_COMPUTE_BIT;

        graph.AddComputePass(m_Name,
            {
                TransientResource::Image(RS::NORMAL, VK_FORMAT_R16G16B16A16_SFLOAT, 0, {}, TransientImageType::StorageImage),
                TransientResource::Image(RS::DEPTH, VK_FORMAT_D32_SFLOAT, 2),
                TransientResource::Image(m_InputName, VK_FORMAT_R16G16B16A16_SFLOAT, 3, {}, TransientImageType::StorageImage)
            },
            {
                TransientResource::Image(m_OutputName, VK_FORMAT_R16G16B16A16_SFLOAT, 4, {}, TransientImageType::StorageImage)
            },
            pipelineDesc,
            [step = m_StepSize](ComputeExecutionContext& ctx) {
                auto size = ctx.GetDisplaySize();
                ctx.Dispatch("svgf_atrous.comp", size.x / 8 + (size.x % 8 != 0), size.y / 8 + (size.y % 8 != 0), 1, (int&)step);
            },
            "Atrous_Standard"
        );
    }

}
