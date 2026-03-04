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
            RGResourceHandle output; 
            RGResourceHandle outMoments; 
            RGResourceHandle depth;
            RGResourceHandle normal;
            RGResourceHandle prevDepth;
            RGResourceHandle prevNormal;
        };

        std::string temporalMomentsName = config.prefix + "_TemporalMoments";

        graph.AddComputePass<TemporalData>(config.prefix + "_Temporal",
            [&](TemporalData& data, RenderGraph::PassBuilder& builder)
            {
                // BINDING ORDER MUST MATCH SHADER SET 2
                data.cur            = builder.ReadCompute(config.inputName);        // Binding 0
                data.motion         = builder.ReadCompute(RS::Motion);             // Binding 1
                data.history        = builder.ReadHistory(config.historyBaseName); // Binding 2
                data.historyMoments = builder.ReadHistory(config.prefix + "Moments"); // Binding 3
                
                data.output         = builder.WriteStorage(config.prefix + "_TemporalColor").Format(VK_FORMAT_R16G16B16A16_SFLOAT).SaveAsHistory(config.historyBaseName); // Binding 4
                data.outMoments     = builder.WriteStorage(temporalMomentsName).Format(VK_FORMAT_R16G16B16A16_SFLOAT).SaveAsHistory(config.prefix + "Moments"); // Binding 5
                
                data.depth          = builder.ReadCompute(RS::Depth);              // Binding 6
                data.normal         = builder.ReadCompute(RS::Normal);             // Binding 7
                data.prevDepth      = builder.ReadHistory(RS::Depth);              // Binding 8
                data.prevNormal     = builder.ReadHistory(RS::Normal);             // Binding 9
            },
            [config](const TemporalData& data, ComputeExecutionContext& ctx)
            {
                ctx.BindPipeline("SVGF_Temporal");
                ctx.Dispatch("SVGF_Temporal", (ctx.GetGraph().GetWidth() + 15) / 16, (ctx.GetGraph().GetHeight() + 15) / 16);
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
                    data.input   = builder.ReadCompute(currentInput);        // Binding 0
                    data.moments = builder.ReadCompute(temporalMomentsName); // Binding 1
                    data.normal  = builder.ReadCompute(RS::Normal);          // Binding 2
                    data.depth   = builder.ReadCompute(RS::Depth);           // Binding 3
                    data.output  = builder.WriteStorage(outputName).Format(VK_FORMAT_R16G16B16A16_SFLOAT); // Binding 4
                },
                [i](const AtrousData& data, ComputeExecutionContext& ctx)
                {
                    int step = 1 << i;
                    ctx.BindPipeline("SVGF_Atrous");
                    ctx.PushConstants(VK_SHADER_STAGE_ALL, step);
                    ctx.Dispatch("SVGF_Atrous", (ctx.GetGraph().GetWidth() + 15) / 16, (ctx.GetGraph().GetHeight() + 15) / 16);
                }
            );
            currentInput = outputName;
        }
    }
}
