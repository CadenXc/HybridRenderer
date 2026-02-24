#include "pch.h"
#include "RayQueryRenderPath.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Passes/StandardPasses.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"
#include "Scene/Scene.h"
#include "Scene/Model.h"
#include "Core/Application.h"

namespace Chimera
{
    RayQueryRenderPath::RayQueryRenderPath(VulkanContext& context)
        : RenderPath(std::shared_ptr<VulkanContext>(&context, [](VulkanContext*){}))
    {
    }

    RayQueryRenderPath::~RayQueryRenderPath()
    {
        m_RenderGraph.reset();
    }

    VkSemaphore RayQueryRenderPath::Render(const RenderFrameInfo& frameInfo)
    {
        if (m_NeedsResize)
        {
            if (!m_Context) return VK_NULL_HANDLE;
            m_RenderGraph = std::make_unique<RenderGraph>(*m_Context, m_Width, m_Height);
            m_NeedsResize = false;
        }

        if (!m_RenderGraph) return VK_NULL_HANDLE;
        
        m_RenderGraph->Reset();

        auto scene = GetSceneShared();
        
        // [FIX] Only add pass if scene and TLAS exist
        if (scene && scene->GetTLAS() != VK_NULL_HANDLE)
        {
            struct PassData { RGResourceHandle output, depth; };
            m_RenderGraph->AddPass<PassData>("RayQueryPass",
                [&](PassData& data, RenderGraph::PassBuilder& builder)
                {
                    data.output = builder.Write(RS::FinalColor, VK_FORMAT_R16G16B16A16_SFLOAT);
                    data.depth  = builder.Write(RS::Depth, VK_FORMAT_D32_SFLOAT);
                },
                [=](const PassData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd)
                {
                    GraphicsExecutionContext ctx(reg.graph, reg.pass, cmd);
                    
                    GraphicsPipelineDescription desc{};
                    desc.name = "RayQuery_Pipeline";
                    desc.vertex_shader = "forward/forward.vert";
                    desc.fragment_shader = "raytracing/rayquery.frag";
                    
                    ctx.DrawMeshes(desc, scene.get());
                }
            );
        }

        StandardPasses::AddLinearizeDepthPass(*m_RenderGraph);

        m_RenderGraph->Compile();
        return m_RenderGraph->Execute(frameInfo.commandBuffer);
    }
}
