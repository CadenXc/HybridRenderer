#include "pch.h"
#include "RaytracePass.h"
#include "Renderer/Graph/RaytracingExecutionContext.h"
#include "Renderer/Graph/ResourceNames.h"

namespace Chimera {

    struct RaytracePushConstants
    {
        glm::vec4 clearColor;
        glm::vec3 lightPos;
        float lightIntensity;
        int frameCount;
    };

    RaytracePass::RaytracePass(std::shared_ptr<Scene> scene, uint32_t& frameCount)
        : RenderGraphPass("Raytrace Pass"), m_Scene(scene), m_FrameCount(frameCount)
    {
    }

    void RaytracePass::Setup(RenderGraph& graph)
    {
        RaytracingPipelineDescription rtPipelineDesc{};
        rtPipelineDesc.name = "Raytrace Pipeline";
        rtPipelineDesc.raygen_shader = "raygen.rgen";
        rtPipelineDesc.miss_shaders = { "miss.rmiss" };
        rtPipelineDesc.hit_shaders = { HitShader{ .closest_hit = "closesthit.rchit" } };
        rtPipelineDesc.push_constants.shader_stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR;
        rtPipelineDesc.push_constants.size = sizeof(RaytracePushConstants);

        TransientResource topLevelAS(TransientResourceType::AccelerationStructure, RS::SCENE_AS);
        topLevelAS.as.binding = 0;
        topLevelAS.as.handle = m_Scene->GetTLAS();

        auto rtOutput = TransientResource::Image(RS::RT_OUTPUT, VK_FORMAT_R8G8B8A8_UNORM, 1, { {0.0f, 0.0f, 0.0f, 0.0f} }, TransientImageType::StorageImage);
        auto vb       = TransientResource::Buffer("SceneVB", 3, m_Scene->GetVertexBuffer());
        auto ib       = TransientResource::Buffer("SceneIB", 4, m_Scene->GetIndexBuffer());

        auto scene = m_Scene;
        uint32_t& frameCountRef = m_FrameCount; 

        graph.AddRaytracingPass(m_Name,
            { topLevelAS, vb, ib }, 
            { rtOutput },  
            rtPipelineDesc,
            [scene, &frameCountRef](ExecuteRaytracingCallback execute) {
                execute("Raytrace Pipeline", [scene, &frameCountRef](RaytracingExecutionContext& ctx) {
                    auto size = ctx.GetDisplaySize();
                    
                    RaytracePushConstants pc{};
                    pc.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                    pc.lightPos = glm::vec3(5.0f, 5.0f, 5.0f);
                    pc.lightIntensity = 10.0f;
                    pc.frameCount = ++frameCountRef; 
                    
                    vkCmdPushConstants(ctx.GetCommandBuffer(), ctx.GetPipelineLayout(), 
                        VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR, 
                        0, sizeof(pc), &pc);

                    ctx.TraceRays(size.x, size.y); 
                });
            }
        );
    }

}
