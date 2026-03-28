#include "pch.h"
#include "TAAPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ComputeExecutionContext.h"
#include "Core/Application.h"

namespace Chimera
{
    static std::string s_BufferNames[2] = { "TAA_Ping", "TAA_Pong" };

    TAAPass::TAAPass()
    {
    }

    void TAAPass::Setup(PassData& data, RenderGraph::PassBuilder& builder)
    {
        // current input color
        data.current = builder.ReadCompute(RS::FinalColor);
        
        // previous frame's accumulated result (Feedback)
        data.history = builder.ReadHistory("TAAOutput");
        
        data.motion  = builder.ReadCompute(RS::Motion);
        data.depth   = builder.ReadCompute(RS::Depth);

        // write new accumulated result and save it for the next frame
        data.output  = builder.WriteStorage("TAAOutput")
                              .Format(VK_FORMAT_R16G16B16A16_SFLOAT)
                              .SaveAsHistory("TAAOutput");
    }

    void TAAPass::Execute(const PassData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd)
    {
        ComputeExecutionContext ctx(reg.graph, reg.pass, cmd);
        ctx.BindPipeline("TAA_Comp");
        ctx.Dispatch("TAA_Comp", 
            (ctx.GetGraph().GetWidth() + 15) / 16, 
            (ctx.GetGraph().GetHeight() + 15) / 16);
    }
}
