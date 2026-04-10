#include "pch.h"
#include "RTShadowPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/RaytracingExecutionContext.h"

namespace Chimera
{
RTShadowPass::RTShadowPass(std::shared_ptr<Scene> scene) : m_Scene(scene) {}

/**
 * @brief Setup Phase: Declare resources for the Ray Traced Shadow/AO pass.
 * 
 * We use a high-precision storage image (R16G16B16A16_SFLOAT) to pack the raw 1-spp signals:
 * - R Channel: Soft Shadow visibility (Next Event Estimation results).
 * - G Channel: Ambient Occlusion (Hemisphere sampling results).
 * 
 * These noisy signals are subsequently processed by the SVGF (Spatiotemporal Variance-Guided Filtering)
 * subsystem to produce high-quality, temporally stable output.
 */
void RTShadowPass::Setup(PassData& data, RenderGraph::PassBuilder& builder)
{
    // Write access to the Shadow/AO combined buffer
    data.output = builder.WriteStorage(RS::ShadowAO)
                      .Format(VK_FORMAT_R16G16B16A16_SFLOAT);

    // Read access to G-Buffer attributes needed for ray reconstruction
    data.normal = builder.ReadCompute(RS::Normal);
    data.depth = builder.ReadCompute(RS::Depth);
}

/**
 * @brief Execution Phase: Dispatch the hardware ray tracing pipeline.
 * 
 * This pass utilizes the VK_KHR_ray_tracing_pipeline extension but prioritizes 
 * Hardware Ray Query (Inline Ray Tracing) within the Raygen shader for shadow visibility.
 * This approach offers better performance by reducing the overhead of Shader Binding Table (SBT) 
 * lookups and state transitions associated with traceRayEXT calls.
 */
void RTShadowPass::Execute(const PassData& data, RenderGraphRegistry& reg,
                           VkCommandBuffer cmd)
{
    // RaytracingExecutionContext handles the binding of Set 0 (UBO) and Set 1 (Global AS/Buffers).
    RaytracingExecutionContext ctx(reg.graph, reg.pass, cmd);

    RaytracingPipelineDescription desc;
    desc.raygen_shader = "RT_Shadow_Gen"; // Maps to rt_shadow.rgen

    // Note: While the current rgen uses Ray Query, we provide standard Miss and Hit groups
    // to ensure pipeline compatibility and support for potential future traceRayEXT transitions.
    // Index 0: Radiance Miss (used for skybox/AO fallbacks)
    // Index 1: Shadow Miss (used for binary visibility)
    desc.miss_shaders = {"Raytrace_Miss", "Shadow_Miss"};

    // Closest Hit group for material evaluation (used if secondary rays are required)
    desc.hit_shaders = {{"Raytrace_Hit", "", ""}};

    ctx.BindPipeline(desc);

    // Dispatch rays for the entire viewport. 
    // Each thread corresponds to one pixel in the output Shadow/AO buffer.
    ctx.TraceRays(reg.graph.GetWidth(), reg.graph.GetHeight());
}
} // namespace Chimera

