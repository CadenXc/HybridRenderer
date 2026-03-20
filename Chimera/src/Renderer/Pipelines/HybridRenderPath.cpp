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
    HybridRenderPath::HybridRenderPath(VulkanContext& context)
        : RenderPath(context.GetShared())
    {
    }

    void HybridRenderPath::BuildGraph(RenderGraph& graph, std::shared_ptr<Scene> scene)
    {
        // 0. Depth Prepass (Early-Z Optimization)
        DepthPrepass::AddToGraph(graph, scene);

        // 1. G-Buffer Pass (Uses EQUAL depth test)
        GBufferPass::AddToGraph(graph, scene);

        bool rtSupported = m_Context->IsRayTracingSupported();
        uint32_t renderFlags = Application::Get().GetFrameContext().RenderFlags;
        bool useSVGF = (renderFlags & RENDER_FLAG_SVGF_BIT) != 0;

        // 2. Ray Tracing Passes (Shadows, Reflections, GI)
        bool hasTLAS = scene && scene->GetTLAS() != VK_NULL_HANDLE;
        if (rtSupported && hasTLAS) 
        {
            RTShadowAOPass::AddToGraph(graph, scene);
            RTReflectionPass::AddToGraph(graph, scene);
            RTDiffuseGIPass::AddToGraph(graph, scene);
        }

        // 3. SVGF Denoising Passes (Conditional)
        if (rtSupported && scene && useSVGF) 
        {
            SVGFPass::AddToGraph(graph, scene, {
                .inputName = "CurColor",
                .prefix = "Shadow",
                .historyBaseName = "ShadowAccum"
            });

            SVGFPass::AddToGraph(graph, scene, {
                .inputName = "ReflectionRaw",
                .prefix = "Refl",
                .historyBaseName = "ReflAccum"
            });

            SVGFPass::AddToGraph(graph, scene, {
                .inputName = "GIRaw",
                .prefix = "GI",
                .historyBaseName = "GIAccum"
            });
        }

        // 4. Composition Pass (Connect either SVGF output or Raw RT output)
        CompositionPass::AddToGraph(graph, {
            .shadowName     = useSVGF ? "Shadow_Filtered_Final" : "CurColor",
            .reflectionName = useSVGF ? "Refl_Filtered_Final"   : "ReflectionRaw",
            .giName         = useSVGF ? "GI_Filtered_Final"     : "GIRaw"
        });

        // 5. Post Processing (Static Chain)
        // 5.1 TAA (Always outputs TAAOutput)
        TAAPass::AddToGraph(graph);

        // 5.2 Final Post & Tone Mapping (Outputs RS::RENDER_OUTPUT)
        PostProcessPass::AddToGraph(graph, "TAAOutput");
    }

    void HybridRenderPath::OnImGui()
    {
        // Control is handled by EditorLayer to prevent state conflicts
    }
}
