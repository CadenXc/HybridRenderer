#include "pch.h"
#include "ForwardRenderPath.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Passes/ForwardPass.h"
#include "Renderer/Passes/SkyboxPass.h"
#include "Renderer/Passes/TAAPass.h"
#include "Renderer/Passes/PostProcessPass.h"
#include "Core/Application.h"

namespace Chimera
{
ForwardRenderPath::ForwardRenderPath(VulkanContext& context)
    : RenderPath(context.GetShared())
{
}

void ForwardRenderPath::BuildGraph(RenderGraph& graph,
                                   std::shared_ptr<Scene> scene)
{
    graph.AddPass<ForwardPass>(scene);

    const bool taaEnabled =
        Application::Get().GetFrameContext().RenderFlags & RenderFlags_TAABit;

    if (taaEnabled)
    {
        graph.AddPass<TAAPass>();
        graph.AddPass<PostProcessPass>("TAAOutput");
    }
    else
    {
        graph.AddPass<PostProcessPass>(RS::FinalColor);
    }

}
} // namespace Chimera
