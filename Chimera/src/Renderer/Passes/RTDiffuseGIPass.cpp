#include "pch.h"
#include "RTDiffuseGIPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/RaytracingExecutionContext.h"
#include "Scene/Scene.h"

namespace Chimera
{
RTDiffuseGIPass::RTDiffuseGIPass(std::shared_ptr<Scene> scene) : m_Scene(scene)
{
}

void RTDiffuseGIPass::Setup(PassData& data, RenderGraph::PassBuilder& builder)
{
    data.output =
        builder.WriteStorage("GIRaw").Format(VK_FORMAT_R16G16B16A16_SFLOAT);
    data.normal = builder.Read(RS::Normal);
    data.depth = builder.Read(RS::Depth);
    data.material = builder.Read(RS::MaterialParams);
}

void RTDiffuseGIPass::Execute(const PassData& data, RenderGraphRegistry& reg,
                              VkCommandBuffer cmd)
{
    if (!m_Scene) return;

    RaytracingExecutionContext ctx(reg.graph, reg.pass, cmd);

    RaytracingPipelineDescription desc;
    desc.raygen_shader = "DiffuseGI_Gen";
    desc.miss_shaders = {"Raytrace_Miss"};
    desc.hit_shaders = {{"Raytrace_Hit", "", ""}};

    int skyboxIndex = (int)m_Scene->GetSkyboxTextureIndex();

    ctx.BindPipeline(desc);
    ctx.PushConstants(VK_SHADER_STAGE_ALL, skyboxIndex);
    ctx.TraceRays(reg.graph.GetWidth(), reg.graph.GetHeight());
}
} // namespace Chimera
