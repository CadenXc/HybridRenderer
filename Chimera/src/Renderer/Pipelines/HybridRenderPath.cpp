#include "pch.h"
#include "HybridRenderPath.h"
#include <imgui.h>
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Passes/GBufferPass.h"
#include "Renderer/Passes/DepthPrepass.h"
#include "Renderer/Passes/RTShadowPass.h"
#include "Renderer/Passes/RTAOPass.h"
#include "Renderer/Passes/RTReflectionPass.h"
#include "Renderer/Passes/RTDiffuseGIPass.h"
#include "Renderer/Passes/SVGFPass.h"
#include "Renderer/Passes/CompositionPass.h"
#include "Renderer/Passes/TAAPass.h"
#include "Renderer/Passes/PostProcessPass.h"
#include "Renderer/Passes/StandardPasses.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"
#include "Renderer/Backend/Renderer.h"
#include "Core/Application.h"
#include "Utils/VulkanBarrier.h"

namespace Chimera
{
HybridRenderPath::HybridRenderPath(VulkanContext& context)
    : RenderPath(context.GetShared())
{
}

void HybridRenderPath::BuildGraph(RenderGraph& graph,
                                  std::shared_ptr<Scene> scene)
{
    graph.AddPass<GBufferPass>(scene);

    bool rtSupported = m_Context->IsRayTracingSupported();
    RenderFlags renderFlags = Application::Get().GetFrameContext().RenderFlags;

    bool useSVGFMaster = renderFlags & RenderFlags_SVGFBit;
    bool doTemporal = renderFlags & RenderFlags_SVGFTemporalBit;
    bool doSpatial = renderFlags & RenderFlags_SVGFSpatialBit;

    // 2. Ray Tracing Passes (Shadows, AO, Reflections, GI)
    bool hasTLAS = scene && scene->GetTLAS() != VK_NULL_HANDLE;

    bool useRayTracing = rtSupported && hasTLAS;

    bool svgfActive = useRayTracing && useSVGFMaster && (doTemporal || doSpatial);

    if (useRayTracing)
    {
        graph.AddPass<RTShadowPass>(scene);

        graph.AddPass<RTReflectionPass>(scene);
        graph.AddPass<RTDiffuseGIPass>(scene);
    }
    else
    {
        VkClearColorValue fullyVisible = {{1.0f, 1.0f, 0.0f, 0.0f}};

        VkClearColorValue black = {{0.0f, 0.0f, 0.0f, 0.0f}};

        StandardPasses::AddClearPass(graph, RS::ShadowAO, fullyVisible);

        StandardPasses::AddClearPass(graph, "ReflectionRaw", black);

        StandardPasses::AddClearPass(graph, "GIRaw", black);
    }

    // 3. SVGF Denoising Passes (Conditional)
    if (svgfActive)
    {
        SVGFPass::Config baseConfig;
        baseConfig.temporalEnabled = doTemporal;
        baseConfig.spatialEnabled = doSpatial;

        // --- [PACKED] Shadow and AO SVGF ---
        SVGFPass::Config shadowAOConfig = baseConfig;
        shadowAOConfig.inputName = RS::ShadowAO; // Packed input
        shadowAOConfig.prefix = "ShadowAO";
        shadowAOConfig.historyBaseName = "ShadowAOAccum";
        shadowAOConfig.useAlbedoDemod =
            false; // No albedo for raw visibility signals
        graph.AddPass<SVGFPass>(scene, shadowAOConfig);

        // --- Reflection SVGF ---
        SVGFPass::Config reflConfig = baseConfig;
        reflConfig.inputName = "ReflectionRaw";
        reflConfig.prefix = "Refl";
        reflConfig.historyBaseName = "ReflAccum";
        reflConfig.useAlbedoDemod = true;
        graph.AddPass<SVGFPass>(scene, reflConfig);

        // --- GI SVGF ---
        SVGFPass::Config giConfig = baseConfig;
        giConfig.inputName = "GIRaw";
        giConfig.prefix = "GI";
        giConfig.historyBaseName = "GIAccum";
        giConfig.useAlbedoDemod = true;
        graph.AddPass<SVGFPass>(scene, giConfig);
    }

    // 4. Composition Pass
    CompositionPass::Config compConfig;
    compConfig.shadowName =
        svgfActive ? "ShadowAO_Filtered_Final" : RS::ShadowAO;
    compConfig.aoName = svgfActive
                            ? "ShadowAO_Filtered_Final"
                            : RS::ShadowAO; // Uses G channel inside shader
    compConfig.reflectionName =
        svgfActive ? "Refl_Filtered_Final" : "ReflectionRaw";
    compConfig.giName = svgfActive ? "GI_Filtered_Final" : "GIRaw";

    graph.AddPass<CompositionPass>(compConfig);

    // 5. Post Processing
    graph.AddPass<PostProcessPass>(RS::FinalColor);
}

void HybridRenderPath::OnImGui() {}
} // namespace Chimera
