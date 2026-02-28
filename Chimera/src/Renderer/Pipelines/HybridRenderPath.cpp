#include "pch.h"
#include "HybridRenderPath.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Passes/GBufferPass.h"
#include "Renderer/Passes/RTShadowAOPass.h"
#include "Renderer/Passes/RTReflectionPass.h"
#include "Renderer/Passes/RTDiffuseGIPass.h"
#include "Renderer/Passes/SVGFPass.h"
#include "Renderer/Passes/CompositionPass.h"
#include "Renderer/Passes/BloomPass.h"
#include "Renderer/Passes/TAAPass.h"
#include "Renderer/Passes/StandardPasses.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"
#include "Renderer/Backend/Renderer.h"
#include "Core/Application.h"
#include "Utils/VulkanBarrier.h"

namespace Chimera
{
    HybridRenderPath::HybridRenderPath(VulkanContext& context)
        : RenderPath(std::shared_ptr<VulkanContext>(&context, [](VulkanContext*){}))
    {
    }

    void HybridRenderPath::BuildGraph(RenderGraph& graph, std::shared_ptr<Scene> scene)
    {
        // 1. G-Buffer Pass
        GBufferPass::AddToGraph(graph, scene);

        // 2. Ray Tracing Passes (Shadows, Reflections, GI)
        bool hasTLAS = scene && scene->GetTLAS() != VK_NULL_HANDLE;
        if (hasTLAS) 
        {
            RTShadowAOPass::AddToGraph(graph, scene);
            RTReflectionPass::AddToGraph(graph, scene);
            RTDiffuseGIPass::AddToGraph(graph, scene);
        }

        // 3. SVGF Denoising Passes [REFACTORED: Config Descriptor Pattern]
        if (scene) 
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

        // 4. Composition Pass [REFACTORED: Config Descriptor Pattern]
        CompositionPass::AddToGraph(graph, {
            .shadowName = "Shadow_Filtered_4",
            .reflectionName = "Refl_Filtered_4",
            .giName = "GI_Filtered_4"
        });

        // 5. Post-processing
        StandardPasses::AddLinearizeDepthPass(graph);
        // BloomPass::AddToGraph(graph);
        // TAAPass::AddToGraph(graph);
    }
}
