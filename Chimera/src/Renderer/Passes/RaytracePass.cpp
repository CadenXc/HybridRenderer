#include "pch.h"
#include "RaytracePass.h"
#include "Renderer/Graph/RaytracingExecutionContext.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Resources/Buffer.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Backend/ShaderResourceNames.h"
#include "Renderer/Resources/ResourceManager.h"

namespace Chimera {

    RaytracePass::RaytracePass(std::shared_ptr<Scene> scene, uint32_t& frameCount)
        : RenderGraphPass("Raytrace Pass"), m_Scene(scene), m_FrameCount(frameCount)
    {
    }

    void RaytracePass::Setup(RenderGraph& graph)
    {
        VkAccelerationStructureKHR tlas = m_Scene->GetTLAS();
        auto scene = m_Scene;
        uint32_t& frameCountRef = m_FrameCount;

        RaytracingPassSpecification spec;
        spec.Name = m_Name;
        spec.Dependencies = { 
            TransientResource::AccelerationStructure(RS::SCENE_AS, tlas),
            TransientResource::Buffer("MaterialBuffer", m_Scene->GetMaterialBuffer()),
            TransientResource::Buffer("InstanceDataBuffer", m_Scene->GetInstanceDataBuffer()),
            TransientResource::Sampler("TextureArray", 0, (uint32_t)ResourceManager::Get()->GetTextures().size())
        };
        spec.Outputs = { 
            TransientResource::Image(RS::RT_OUTPUT, VK_FORMAT_R8G8B8A8_UNORM, 0xFFFFFFFF, {{0,0,0,0}}, TransientImageType::StorageImage)
        };
        spec.Pipeline = CreatePipelineDescription();
        spec.ShaderLayout = "RT_Standard";
        
        spec.Callback = [scene, &frameCountRef](ExecuteRaytracingCallback execute) {
            execute("Raytrace Pipeline", [scene, &frameCountRef](RaytracingExecutionContext& ctx) {
                auto size = ctx.GetDisplaySize();
                RaytracePushConstants pc{};
                pc.frameCount = ++frameCountRef; 
                pc.skyboxIndex = scene->GetSkyboxTextureIndex();
                vkCmdPushConstants(ctx.GetCommandBuffer(), ctx.GetPipelineLayout(), VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR, 0, sizeof(RaytracePushConstants), &pc);
                ctx.TraceRays(size.x, size.y); 
            });
        };

        graph.AddRaytracingPass(spec);
    }

    RaytracingPipelineDescription RaytracePass::CreatePipelineDescription()
    {
        RaytracingPipelineDescription rtPipelineDesc{};
        rtPipelineDesc.name = "Raytrace Pipeline";
        rtPipelineDesc.raygen_shader = Shaders::RayGen;
        rtPipelineDesc.miss_shaders = { Shaders::Miss, Shaders::ShadowMiss };
        rtPipelineDesc.hit_shaders = { HitShader{ .closest_hit = Shaders::ClosestHit } };
        rtPipelineDesc.push_constants.shader_stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR;
        rtPipelineDesc.push_constants.size = sizeof(RaytracePushConstants); 
        return rtPipelineDesc;
    }

}