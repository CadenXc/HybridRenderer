#include "pch.h"
#include "PostProcessPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"

namespace Chimera::PostProcessPass
{
    struct PassData
    {
        RGResourceHandle input;
        RGResourceHandle output;
    };

    void AddToGraph(RenderGraph& graph, const std::string& inputName)
    {
        graph.AddPass<PassData>("PostProcessPass",
            [&](PassData& data, RenderGraph::PassBuilder& builder)
            {
                data.input = builder.Read(inputName);
                data.output = builder.Write(RS::RENDER_OUTPUT);
            },
            [](const PassData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd)
            {
                GraphicsExecutionContext ctx(reg.graph, reg.pass, cmd);
                
                ctx.SetViewport(0, 0, (float)reg.graph.GetWidth(), (float)reg.graph.GetHeight());
                ctx.SetScissor(0, 0, reg.graph.GetWidth(), reg.graph.GetHeight());

                GraphicsPipelineDescription desc{ 
                    "PostProcess", 
                    "common/fullscreen.vert", 
                    "postprocess/postprocess.frag", 
                    false, 
                    false 
                };

                ctx.BindPipeline(desc);
                ctx.DrawMeshes(desc, nullptr);
            }
        );
    }
}
