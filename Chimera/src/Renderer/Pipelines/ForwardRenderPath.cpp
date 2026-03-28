#include "pch.h"
#include "ForwardRenderPath.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Passes/ForwardPass.h"
#include "Renderer/Passes/SkyboxPass.h"
#include "Renderer/Passes/TAAPass.h"
#include "Renderer/Passes/PostProcessPass.h"

namespace Chimera
{
    ForwardRenderPath::ForwardRenderPath(VulkanContext &context)
        : RenderPath(context.GetShared())
    {
    }

    void ForwardRenderPath::BuildGraph(RenderGraph &graph, std::shared_ptr<Scene> scene)
    {
        // 2. Scene Rendering (Outputs RS::FinalColor, uses depth testing)
        graph.AddPass<ForwardPass>(scene);

        // 3. Resolve Temporal Aliasing (Outputs TAAOutput)
        graph.AddPass<TAAPass>();

        // 4. Final Composition & Tone Mapping (Outputs RS::RENDER_OUTPUT)
        graph.AddPass<PostProcessPass>("TAAOutput");
    }
}
