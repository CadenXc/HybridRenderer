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
    // MUST STRICTLY MATCH binding order in composition.frag (Set 2, Bindings
    // 0-11)
    data.albedo = builder.Read(RS::Albedo); // 0
    data.normal = builder.Read(RS::Normal); // 1
    data.material = builder.Read(RS::Material); // 2
    data.motion = builder.Read(RS::Motion); // 3
    data.depth = builder.Read(RS::Depth); // 4
    data.emissive = builder.Read(RS::Emissive); // 5

    data.gi_raw = builder.Read(m_Config.giName); // 6
    data.reflection_raw = builder.Read(m_Config.reflectionName); // 7
    data.shadow_raw = builder.Read(m_Config.shadowName); // 8
    data.ao_raw = builder.Read(m_Config.aoName); // 9
    data.shadow_debug_raw = builder.Read("ShadowRaw"); // 10
    data.shadow_moments = builder.Read("Shadow_TemporalMoments"); // 11

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

    struct PushConstants
    {
        int skyboxIndex;
        int displayMode;
    } pc;

    pc.skyboxIndex = -1;
    if (auto scene = ResourceManager::Get().GetActiveScene())
    {
        pc.skyboxIndex = (int)scene->GetSkyboxTextureIndex();
    }
    pc.displayMode = (int)Application::Get().GetFrameContext().DisplayMode;

    ctx.BindPipeline(desc);
    ctx.PushConstants(VK_SHADER_STAGE_ALL, pc);
    ctx.DrawMeshes(desc, nullptr);
}
} // namespace Chimera
