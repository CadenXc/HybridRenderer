#include "pch.h"
#include "TAAPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ComputeExecutionContext.h"

namespace Chimera::TAAPass
{
    struct PassData
    {
        RGResourceHandle current;
        RGResourceHandle history;
        RGResourceHandle motion;
        RGResourceHandle depth;
        RGResourceHandle bloom;
        RGResourceHandle output;
    };

    void AddToGraph(RenderGraph& graph)
    {
        graph.AddComputePass<PassData>("TAAPass",
            [&](PassData& data, RenderGraph::PassBuilder& builder)
            {
                data.current = builder.ReadCompute(RS::FinalColor);
                data.history = builder.ReadHistory("TAA");
                data.motion  = builder.ReadCompute(RS::Motion);
                data.depth   = builder.ReadCompute(RS::Depth);
                data.bloom   = builder.ReadCompute("BloomBlurV");
                data.output  = builder.WriteStorage("TAAOutput").Format(VK_FORMAT_R16G16B16A16_SFLOAT).SaveAsHistory("TAA");
            },
            [](const PassData& data, ComputeExecutionContext& ctx)
            {
                ctx.BindPipeline("postprocess/taa.comp");
                ctx.Dispatch("postprocess/taa.comp", 
                    (ctx.GetGraph().GetWidth() + 15) / 16, 
                    (ctx.GetGraph().GetHeight() + 15) / 16);
            }
        );
    }
}
