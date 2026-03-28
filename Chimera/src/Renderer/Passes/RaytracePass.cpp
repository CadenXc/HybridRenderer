#include "pch.h"
#include "RaytracePass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RaytracingExecutionContext.h"
#include "Renderer/Graph/RenderGraph.h"

namespace Chimera
{
    RaytracePass::RaytracePass(std::shared_ptr<Scene> scene, bool useAlphaTest)
        : m_Scene(scene), m_UseAlphaTest(useAlphaTest)
    {
    }

    void RaytracePass::Setup(RaytracePassData& data, RenderGraph::PassBuilder& builder)
    {
        data.output = builder.WriteStorage(RS::FinalColor).Format(VK_FORMAT_R16G16B16A16_SFLOAT);
        
        // Ensure motion is written here too (standard for path tracers)
        builder.WriteStorage(RS::Motion).Format(VK_FORMAT_R16G16_SFLOAT);
    }

    void RaytracePass::Execute(const RaytracePassData& data, RaytracingExecutionContext& ctx)
    {
        if (!m_Scene) return;

        RaytracingPipelineDescription desc{};
        desc.raygen_shader = "Raytrace_Gen";
        desc.miss_shaders = { "Raytrace_Miss" };
        desc.hit_shaders = { { "Raytrace_Hit", "Shadow_AnyHit" } };
        
        ctx.BindPipeline(desc);

        int alphaTest = m_UseAlphaTest ? 1 : 0;
        ctx.PushConstants(VK_SHADER_STAGE_ALL, alphaTest);

        ctx.TraceRays(ctx.GetGraph().GetWidth(), ctx.GetGraph().GetHeight(), 1);
    }
}
