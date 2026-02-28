#include "pch.h"
#include "SVGFPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ComputeExecutionContext.h"

namespace Chimera::SVGFPass
{
    void AddToGraph(RenderGraph& graph, std::shared_ptr<Scene> scene, const Config& config)
    {
        // 1. Temporal Accumulation Pass
        struct TemporalData 
        { 
            RGResourceHandle cur; 
            RGResourceHandle motion; 
            RGResourceHandle history; 
            RGResourceHandle historyMoments; 
            RGResourceHandle depth;
            RGResourceHandle normal;
            RGResourceHandle output; 
            RGResourceHandle outMoments; 
        };

        std::string temporalMomentsName = config.prefix + "_TemporalMoments";

        graph.AddComputePass<TemporalData>(config.prefix + "_Temporal",
            [&](TemporalData& data, RenderGraph::PassBuilder& builder)
            {
                data.cur            = builder.ReadCompute(config.inputName);
                data.motion         = builder.ReadCompute(RS::Motion);
                data.history        = builder.ReadHistory(config.historyBaseName);
                data.historyMoments = builder.ReadHistory(config.prefix + "Moments");
                data.depth          = builder.ReadCompute(RS::Depth);
                data.normal         = builder.ReadCompute(RS::Normal);
                
                builder.ReadHistory(RS::Depth); 
                builder.ReadHistory(RS::Normal);

                data.output         = builder.WriteStorage(config.prefix + "_TemporalColor").Format(VK_FORMAT_R16G16B16A16_SFLOAT).SaveAsHistory(config.historyBaseName);
                data.outMoments     = builder.WriteStorage(temporalMomentsName).Format(VK_FORMAT_R16G16B16A16_SFLOAT).SaveAsHistory(config.prefix + "Moments");
            },
            [config](const TemporalData& data, ComputeExecutionContext& ctx)
            {
                ctx.BindPipeline("postprocess/svgf/temporal.comp");
                ctx.Dispatch("postprocess/svgf/temporal.comp", (ctx.GetGraph().GetWidth() + 15) / 16, (ctx.GetGraph().GetHeight() + 15) / 16);
            }
        );

        // 2. A-trous Filtering
        std::string currentInput = config.prefix + "_TemporalColor";
        for (int i = 0; i < config.atrousIterations; ++i)
        {
            struct AtrousData 
            { 
                RGResourceHandle input; 
                RGResourceHandle moments; 
                RGResourceHandle normal; 
                RGResourceHandle depth; 
                RGResourceHandle output; 
            };
            
            std::string outputName = config.prefix + "_Filtered_" + std::to_string(i);
            
            graph.AddComputePass<AtrousData>(config.prefix + "_Atrous_" + std::to_string(i),
                [&, temporalMomentsName, outputName](AtrousData& data, RenderGraph::PassBuilder& builder)
                {
                    data.input   = builder.ReadCompute(currentInput);
                    data.moments = builder.ReadCompute(temporalMomentsName);
                    data.normal  = builder.ReadCompute(RS::Normal);
                    data.depth   = builder.ReadCompute(RS::Depth);
                    data.output  = builder.WriteStorage(outputName).Format(VK_FORMAT_R16G16B16A16_SFLOAT);
                },
                [i](const AtrousData& data, ComputeExecutionContext& ctx)
                {
                    int step = 1 << i;
                    ctx.BindPipeline("postprocess/svgf/atrous.comp");
                    ctx.PushConstants(VK_SHADER_STAGE_ALL, step);
                    ctx.Dispatch("postprocess/svgf/atrous.comp", (ctx.GetGraph().GetWidth() + 15) / 16, (ctx.GetGraph().GetHeight() + 15) / 16);
                }
            );
            currentInput = outputName;
        }
    }
}
