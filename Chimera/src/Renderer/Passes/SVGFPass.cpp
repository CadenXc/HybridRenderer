#include "pch.h"
#include "SVGFPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ComputeExecutionContext.h"

namespace Chimera
{
    void SVGFPass::AddToGraph(RenderGraph& graph, std::shared_ptr<Scene> scene, 
                             const std::string& inputName, const std::string& prefix, const std::string& historyBaseName)
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
        };

        std::string temporalMomentsName = prefix + "_TemporalMoments";

        graph.AddComputePass<TemporalData>(prefix + "_Temporal",
            [&](TemporalData& data, RenderGraph::PassBuilder& builder)
            {
                // Match temporal.comp bindings:
                // 0: gCurColor, 1: gMotion, 2: gHistoryColor, 3: gHistoryMoments
                // 4: outColor, 5: outMoments
                data.cur            = builder.ReadCompute(inputName);
                data.motion         = builder.ReadCompute(RS::Motion);
                data.history        = builder.ReadHistory(historyBaseName);
                data.historyMoments = builder.ReadHistory(prefix + "Moments");
                
                data.output         = builder.WriteStorage(prefix + "_TemporalColor",   VK_FORMAT_R16G16B16A16_SFLOAT);
                data.outMoments     = builder.WriteStorage(temporalMomentsName, VK_FORMAT_R16G16B16A16_SFLOAT);
            },
            [prefix](const TemporalData& data, ComputeExecutionContext& ctx)
            {
                ctx.BindPipeline("svgf/temporal.comp");
                ctx.Dispatch("svgf/temporal.comp", (ctx.GetGraph().GetWidth() + 15) / 16, (ctx.GetGraph().GetHeight() + 15) / 16);
            }
        );

        // 2. A-trous Filtering (5 Iterations)
        std::string currentInput = prefix + "_TemporalColor";
        for (int i = 0; i < 5; ++i)
        {
            struct AtrousData 
            { 
                RGResourceHandle input; 
                RGResourceHandle moments; 
                RGResourceHandle normal; 
                RGResourceHandle depth; 
                RGResourceHandle output; 
            };
            
            std::string outputName = prefix + "_Filtered_" + std::to_string(i);
            
            graph.AddComputePass<AtrousData>(prefix + "_Atrous_" + std::to_string(i),
                [&, temporalMomentsName, outputName](AtrousData& data, RenderGraph::PassBuilder& builder)
                {
                    // Match atrous.comp: 
                    // Binding 0: gInputColor (Read)
                    // Binding 1: gInputMoments (Read)
                    // Binding 2: gNormal (Read)
                    // Binding 3: gDepth (Read)
                    // Binding 4: outFiltered (Write)
                    data.input   = builder.ReadCompute(currentInput);
                    data.moments = builder.ReadCompute(temporalMomentsName);
                    data.normal  = builder.ReadCompute(RS::Normal);
                    data.depth   = builder.ReadCompute(RS::Depth);
                    data.output  = builder.WriteStorage(outputName, VK_FORMAT_R16G16B16A16_SFLOAT);
                },
                [i](const AtrousData& data, ComputeExecutionContext& ctx)
                {
                    int step = 1 << i;
                    ctx.BindPipeline("svgf/atrous.comp");
                    ctx.PushConstants(VK_SHADER_STAGE_ALL, step);
                    ctx.Dispatch("svgf/atrous.comp", (ctx.GetGraph().GetWidth() + 15) / 16, (ctx.GetGraph().GetHeight() + 15) / 16);
                }
            );
            currentInput = outputName;
        }
    }
}