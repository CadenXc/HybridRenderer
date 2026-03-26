#include "pch.h"
#include "CompositionPass.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Scene/Scene.h"

namespace Chimera
{
    CompositionPass::CompositionPass(const Config& config)
        : m_Config(config)
    {
    }

    void CompositionPass::Setup(PassData& data, RenderGraph::PassBuilder& builder)
    {
        data.albedo         = builder.Read(RS::Albedo);        
        data.normal         = builder.Read(RS::Normal);        
        data.material       = builder.Read(RS::Material);      
        data.motion         = builder.Read(RS::Motion);        
        data.depth          = builder.Read(RS::Depth);         
        data.emissive       = builder.Read(RS::Emissive);      
        
        data.gi_raw         = builder.Read(m_Config.giName);         
        data.reflection_raw = builder.Read(m_Config.reflectionName); 
        data.shadow_raw     = builder.Read(m_Config.shadowName);     
        
        data.output         = builder.Write(RS::FinalColor).Format(VK_FORMAT_R16G16B16A16_SFLOAT);
    }

    void CompositionPass::Execute(const PassData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd)
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
}
