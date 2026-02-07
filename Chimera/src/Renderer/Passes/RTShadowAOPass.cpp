#include "pch.h"
#include "RTShadowAOPass.h"
#include "Renderer/Graph/RaytracingExecutionContext.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Backend/ShaderResourceNames.h"
#include "Renderer/Resources/ResourceManager.h"

namespace Chimera {

    RTShadowAOPass::RTShadowAOPass(std::shared_ptr<Scene> scene, uint32_t& frameCount)
        : RenderGraphPass("RT Shadow AO Pass"), m_Scene(scene), m_FrameCount(frameCount)
    {
    }

    void RTShadowAOPass::Setup(RenderGraph& graph)
    {
        RaytracingPassSpecification spec;
        spec.Name = m_Name;
        spec.Dependencies = { 
            TransientResource::AccelerationStructure(RS::SCENE_AS, m_Scene->GetTLAS()),
            TransientResource::Buffer("MaterialBuffer", m_Scene->GetMaterialBuffer()),
            TransientResource::Buffer("InstanceDataBuffer", m_Scene->GetInstanceDataBuffer()),
            TransientResource::Sampler("TextureArray", 0, (uint32_t)ResourceManager::Get()->GetTextures().size()),
            TransientResource::Image(RS::NORMAL, VK_FORMAT_R16G16B16A16_SFLOAT),
            TransientResource::Image(RS::DEPTH, VK_FORMAT_D32_SFLOAT),
            TransientResource::Image(RS::MATERIAL, VK_FORMAT_R8G8B8A8_UNORM)
        };
        spec.Outputs = { 
            TransientResource::Image(RS::RT_SHADOW_AO, VK_FORMAT_R16G16_SFLOAT, 0xFFFFFFFF, {{0.5f, 0.5f, 0, 0}}, TransientImageType::StorageImage),
            TransientResource::Image(RS::RT_REFLECTIONS, VK_FORMAT_R16G16B16A16_SFLOAT, 0xFFFFFFFF, {{0,0,0,0}}, TransientImageType::StorageImage)
        };
        spec.Pipeline = CreatePipelineDescription();
        spec.ShaderLayout = "RT_Standard";
        
        spec.Callback = [](ExecuteRaytracingCallback execute) {
            execute("RT Shadow AO Pipeline", [](RaytracingExecutionContext& ctx) {
                auto size = ctx.GetDisplaySize();
                RaytracePushConstants pc{};
                vkCmdPushConstants(ctx.GetCommandBuffer(), ctx.GetPipelineLayout(), VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 0, sizeof(RaytracePushConstants), &pc);
                ctx.TraceRays(size.x, size.y);
            });
        };

        graph.AddRaytracingPass(spec);
    }

    RaytracingPipelineDescription RTShadowAOPass::CreatePipelineDescription()
    {
        RaytracingPipelineDescription pipelineDesc{};
        pipelineDesc.name = "RT Shadow AO Pipeline";
        pipelineDesc.raygen_shader = Shaders::RTShadowAO;
        pipelineDesc.miss_shaders = { Shaders::Miss, Shaders::ShadowMiss }; 
        pipelineDesc.hit_shaders = { HitShader{ .closest_hit = Shaders::ClosestHit } }; 
        pipelineDesc.push_constants = { VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, sizeof(RaytracePushConstants) }; 
        return pipelineDesc;
    }

}