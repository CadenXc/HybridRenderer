#include "pch.h"
#include "RTShadowAOPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/RaytracingExecutionContext.h"

namespace Chimera
{
    RTShadowAOPass::RTShadowAOPass(std::shared_ptr<Scene> scene)
        : m_Scene(scene)
    {
    }

    void RTShadowAOPass::Setup(PassData& data, RenderGraph::PassBuilder& builder)
    {
        data.output = builder.WriteStorage("CurColor").Format(VK_FORMAT_R16G16B16A16_SFLOAT);
        data.normal = builder.Read(RS::Normal);
        data.depth  = builder.Read(RS::Depth);
    }

    void RTShadowAOPass::Execute(const PassData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd)
    {
        RaytracingExecutionContext ctx(reg.graph, reg.pass, cmd);
        
        RaytracingPipelineDescription desc;
        desc.raygen_shader = "Raygen_Gen";
        desc.miss_shaders = { "Raytrace_Miss", "Shadow_Miss" };
        desc.hit_shaders = { { "Raytrace_Hit", "", "" } };

        ctx.BindPipeline(desc);
        ctx.TraceRays(reg.graph.GetWidth(), reg.graph.GetHeight());
    }
}
