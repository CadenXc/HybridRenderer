#include "pch.h"
#include "CompositionPass.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Core/Application.h"
#include "Scene/Scene.h"

namespace Chimera
{
CompositionPass::CompositionPass(const Config& config) : m_Config(config) {}

void CompositionPass::Setup(PassData& data, RenderGraph::PassBuilder& builder)
{
    data.albedo = builder.Read(RS::Albedo, "gAlbedo");

    data.normal = builder.Read(RS::Normal, "gNormal");

    data.material = builder.Read(RS::MaterialParams, "gMaterialParams");

    data.motion = builder.Read(RS::Motion, "gMotion");

    data.depth = builder.Read(RS::Depth, "gDepth");

    data.emissive = builder.Read(RS::Emissive, "gEmissive");

    data.gi_raw = builder.Read(m_Config.giName, "gGI");

    data.reflection_raw = builder.Read(m_Config.reflectionName, "gReflection");

    data.shadow_raw = builder.Read(m_Config.shadowName, "gShadow");

    data.ao_raw = builder.Read(m_Config.aoName, "gAO");

    data.output =
        builder.Write(RS::FinalColor).Format(VK_FORMAT_R16G16B16A16_SFLOAT);
}

void CompositionPass::Execute(const PassData& data,
                              GraphicsExecutionContext& ctx)
{
    GraphicsPipelineDescription desc{};
    desc.name = "Composition_Pipeline";
    desc.vertex_shader = "Fullscreen_Vert";
    desc.fragment_shader = "Composition_Frag";
    desc.depth_test = false;
    desc.depth_write = false;
    desc.cull_mode = VK_CULL_MODE_NONE;

    ctx.DrawMeshes(desc, nullptr);
}
} // namespace Chimera
