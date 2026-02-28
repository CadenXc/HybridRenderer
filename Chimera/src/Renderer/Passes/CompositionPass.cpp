#include "pch.h"
#include "CompositionPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"

namespace Chimera::CompositionPass
{
    struct PassData 
    { 
        RGResourceHandle albedo, shadow, shadow_raw, reflection, reflection_raw, gi, gi_raw, material, normal, depth, output, motion; 
    };

    void AddToGraph(RenderGraph& graph, const Config& config)
    {
        graph.AddPass<PassData>("Composition",
            [&](PassData& data, RenderGraph::PassBuilder& builder) 
            {
                data.albedo         = builder.Read(RS::Albedo);
                data.shadow         = builder.Read(config.shadowName);
                data.shadow_raw     = builder.Read("Shadow");
                data.reflection     = builder.Read(config.reflectionName);
                data.reflection_raw = builder.Read("ReflectionRaw");
                data.gi             = builder.Read(config.giName);
                data.gi_raw         = builder.Read("GIRaw");
                data.material       = builder.Read(RS::Material);
                data.normal         = builder.Read(RS::Normal);
                data.depth          = builder.Read(RS::Depth);
                data.motion         = builder.Read(RS::Motion);
                data.output         = builder.Write(RS::FinalColor).Format(VK_FORMAT_R16G16B16A16_SFLOAT);
            },
            [](const PassData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd) 
            {
                GraphicsExecutionContext ctx(reg.graph, reg.pass, cmd);
                ctx.DrawMeshes({ "Composition", "common/fullscreen.vert", "postprocess/composition.frag" }, nullptr);
            }
        );
    }
}
