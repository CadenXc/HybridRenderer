#include "pch.h"
#include "BloomPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ComputeExecutionContext.h"

namespace Chimera::BloomPass
{
    void AddToGraph(RenderGraph& graph)
    {
        struct ThresholdData { RGResourceHandle input, output; };
        graph.AddComputePass<ThresholdData>("BloomThreshold",
            [](ThresholdData& data, RenderGraph::PassBuilder& builder) {
                data.input = builder.ReadCompute(RS::FinalColor);
                data.output = builder.WriteStorage("BloomBright").Format(VK_FORMAT_R16G16B16A16_SFLOAT);
            },
            [](const ThresholdData& data, ComputeExecutionContext& ctx) {
                ctx.Dispatch("postprocess/bloom_threshold.comp", 
                    (ctx.GetGraph().GetWidth() + 15) / 16, 
                    (ctx.GetGraph().GetHeight() + 15) / 16);
            }
        );

        struct BlurData { RGResourceHandle input, output; };
        // Horizontal
        graph.AddComputePass<BlurData>("BloomBlurH",
            [](BlurData& data, RenderGraph::PassBuilder& builder) {
                data.input = builder.ReadCompute("BloomBright");
                data.output = builder.WriteStorage("BloomBlurH").Format(VK_FORMAT_R16G16B16A16_SFLOAT);
            },
            [](const BlurData& data, ComputeExecutionContext& ctx) {
                ctx.BindPipeline("postprocess/bloom_blur.comp");
                ctx.PushConstants(VK_SHADER_STAGE_COMPUTE_BIT, 1);
                ctx.Dispatch("postprocess/bloom_blur.comp", 
                    (ctx.GetGraph().GetWidth() + 15) / 16, 
                    (ctx.GetGraph().GetHeight() + 15) / 16);
            }
        );

        // Vertical
        graph.AddComputePass<BlurData>("BloomBlurV",
            [](BlurData& data, RenderGraph::PassBuilder& builder) {
                data.input = builder.ReadCompute("BloomBlurH");
                data.output = builder.WriteStorage("BloomBlurV").Format(VK_FORMAT_R16G16B16A16_SFLOAT);
            },
            [](const BlurData& data, ComputeExecutionContext& ctx) {
                ctx.BindPipeline("postprocess/bloom_blur.comp");
                ctx.PushConstants(VK_SHADER_STAGE_COMPUTE_BIT, 0);
                ctx.Dispatch("postprocess/bloom_blur.comp", 
                    (ctx.GetGraph().GetWidth() + 15) / 16, 
                    (ctx.GetGraph().GetHeight() + 15) / 16);
            }
        );
    }
}
