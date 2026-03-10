#include "pch.h"
#include "TAAPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ComputeExecutionContext.h"
#include "Core/Application.h"

namespace Chimera::TAAPass
{
    struct PassData
    {
        RGResourceHandle current;
        RGResourceHandle motion;
        RGResourceHandle depth;
        RGResourceHandle history;
        RGResourceHandle output;
    };

    // Static buffers to hold history across frames
    static std::string s_BufferNames[2] = { "TAA_Ping", "TAA_Pong" };

    void AddToGraph(RenderGraph& graph)
    {
        uint32_t frameIndex = Application::Get().GetTotalFrameCount();
        int currIdx = frameIndex % 2;
        int prevIdx = (frameIndex + 1) % 2;

        graph.AddComputePass<PassData>("TAAPass",
            [&](PassData& data, RenderGraph::PassBuilder& builder)
            {
                // [CRITICAL] The order here MUST match the binding order in taa.comp!
                // Binding 0: curColor
                data.current = builder.ReadCompute(RS::FinalColor);
                
                // Binding 1: historyColor
                data.history = builder.ReadCompute(s_BufferNames[prevIdx]);
                
                // Binding 2: gMotion
                data.motion  = builder.ReadCompute(RS::Motion);
                
                // Binding 3: gDepth
                data.depth   = builder.ReadCompute(RS::Depth);
                
                // Binding 4: outPingPong
                data.output = builder.WriteStorage(s_BufferNames[currIdx]).Format(VK_FORMAT_R16G16B16A16_SFLOAT).Persistent();
                
                // Binding 5: outFinal
                builder.WriteStorage("TAAOutput").Format(VK_FORMAT_R16G16B16A16_SFLOAT);
            },
            [currIdx](const PassData& data, ComputeExecutionContext& ctx)
            {
                ctx.BindPipeline("TAA_Comp");
                ctx.Dispatch("TAA_Comp", 
                    (ctx.GetGraph().GetWidth() + 15) / 16, 
                    (ctx.GetGraph().GetHeight() + 15) / 16);
            }
        );
    }
}
