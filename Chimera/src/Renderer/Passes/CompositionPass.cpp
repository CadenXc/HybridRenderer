#include "pch.h"
#include "CompositionPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Scene/Scene.h"

namespace Chimera::CompositionPass
{
    struct PassData 
    { 
        RGResourceHandle albedo;
        RGResourceHandle normal;
        RGResourceHandle material;
        RGResourceHandle motion;
        RGResourceHandle depth;
        RGResourceHandle emissive;
        
        RGResourceHandle gi_raw;
        RGResourceHandle reflection_raw;
        RGResourceHandle shadow_raw;
        
        RGResourceHandle output; 
    };

    void AddToGraph(RenderGraph& graph, const Config& config)
    {
        graph.AddPass<PassData>("Composition",
            [&](PassData& data, RenderGraph::PassBuilder& builder) 
            {
                // [PHASE 3] Physical Composition Slots
                data.albedo         = builder.Read(RS::Albedo);        // Binding 0
                data.normal         = builder.Read(RS::Normal);        // Binding 1
                data.material       = builder.Read(RS::Material);      // Binding 2
                data.motion         = builder.Read(RS::Motion);        // Binding 3
                data.depth          = builder.Read(RS::Depth);         // Binding 4
                data.emissive       = builder.Read(RS::Emissive);      // Binding 5
                
                data.gi_raw         = builder.Read(config.giName);         // Binding 6
                data.reflection_raw = builder.Read(config.reflectionName); // Binding 7
                data.shadow_raw     = builder.Read(config.shadowName);     // Binding 8
                
                data.output         = builder.Write(RS::FinalColor).Format(VK_FORMAT_R16G16B16A16_SFLOAT);
            },
            [](const PassData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd) 
            {
                GraphicsExecutionContext ctx(reg.graph, reg.pass, cmd);
                
                GraphicsPipelineDescription desc{};
                desc.name = "Composition_Pipeline";
                desc.vertex_shader = "Fullscreen_Vert";
                desc.fragment_shader = "Composition_Frag";
                desc.depth_test = false;
                desc.depth_write = false;
                desc.cull_mode = VK_CULL_MODE_NONE;

                ctx.BindPipeline(desc);

                // [FIX] Pass skybox index via Push Constant
                struct PushConstants 
                {
                    int skyboxIndex;
                } pc;
                
                pc.skyboxIndex = -1;
                if (auto scene = ResourceManager::Get().GetActiveScene())
                {
                    pc.skyboxIndex = (int)scene->GetSkyboxTextureIndex();
                }
                
                ctx.PushConstants(VK_SHADER_STAGE_ALL, pc);
                ctx.DrawMeshes(desc, nullptr);
            }
        );
    }
}
