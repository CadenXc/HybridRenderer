#include "pch.h"
#include "HybridRenderPath.h"
#include <imgui.h>
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Passes/GBufferPass.h"
#include "Renderer/Passes/DepthPrepass.h"
#include "Renderer/Passes/RTShadowAOPass.h"
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
    HybridRenderPath::HybridRenderPath(VulkanContext &context)
        : RenderPath(context.GetShared())
    {
    }

    void HybridRenderPath::BuildGraph(RenderGraph &graph, std::shared_ptr<Scene> scene)
    {
        // 0. Depth Prepass (Early-Z Optimization)
        graph.AddPass<DepthPrepass>(scene);

        // 1. G-Buffer Pass (Uses EQUAL depth test)
        graph.AddPass<GBufferPass>(scene);

        bool rtSupported = m_Context->IsRayTracingSupported();
        uint32_t renderFlags = Application::Get().GetFrameContext().RenderFlags;
        
        bool useSVGFMaster = (renderFlags & RENDER_FLAG_SVGF_BIT) != 0;
        bool doTemporal = (renderFlags & RENDER_FLAG_SVGF_TEMPORAL_BIT) != 0;
        bool doSpatial = (renderFlags & RENDER_FLAG_SVGF_SPATIAL_BIT) != 0;
        
        // SVGF is active only if the master gate is open AND at least one stage is enabled
        bool svgfActive = useSVGFMaster && (doTemporal || doSpatial);

        // 2. Ray Tracing Passes (Shadows, Reflections, GI)
        bool hasTLAS = scene && scene->GetTLAS() != VK_NULL_HANDLE;
        if (rtSupported && hasTLAS)
        {
            graph.AddPass<RTShadowAOPass>(scene);
            graph.AddPass<RTReflectionPass>(scene);
            graph.AddPass<RTDiffuseGIPass>(scene);
        }

        // 3. SVGF Denoising Passes (Conditional)
        if (rtSupported && scene && svgfActive)
        {
            SVGFPass::Config baseConfig;
            baseConfig.temporalEnabled = doTemporal;
            baseConfig.spatialEnabled = doSpatial;

            SVGFPass::Config shadowConfig = baseConfig;
            shadowConfig.inputName = "CurColor";
            shadowConfig.prefix = "Shadow";
            shadowConfig.historyBaseName = "ShadowAccum";
            graph.AddPass<SVGFPass>(scene, shadowConfig);

            SVGFPass::Config reflConfig = baseConfig;
            reflConfig.inputName = "ReflectionRaw";
            reflConfig.prefix = "Refl";
            reflConfig.historyBaseName = "ReflAccum";
            graph.AddPass<SVGFPass>(scene, reflConfig);

            SVGFPass::Config giConfig = baseConfig;
            giConfig.inputName = "GIRaw";
            giConfig.prefix = "GI";
            giConfig.historyBaseName = "GIAccum";
            graph.AddPass<SVGFPass>(scene, giConfig);
        }

        // 4. Composition Pass (Connect either SVGF output or Raw RT output)
        CompositionPass::Config compConfig;
        compConfig.shadowName = svgfActive ? "Shadow_Filtered_Final" : "CurColor";
        compConfig.reflectionName = svgfActive ? "Refl_Filtered_Final" : "ReflectionRaw";
        compConfig.giName = svgfActive ? "GI_Filtered_Final" : "GIRaw";

        graph.AddPass<CompositionPass>(compConfig);

        // 5. Post Processing (Static Chain)
        // 5.1 TAA (Always outputs TAAOutput)
        graph.AddPass<TAAPass>();

        // 5.2 Final Post & Tone Mapping (Outputs RS::RENDER_OUTPUT)
        graph.AddPass<PostProcessPass>("TAAOutput");
    }

    void HybridRenderPath::OnImGui()
    {
        // Control is handled by EditorLayer to prevent state conflicts
    }
}
