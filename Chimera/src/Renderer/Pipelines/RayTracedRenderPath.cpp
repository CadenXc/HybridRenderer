#include "pch.h"
#include "RayTracedRenderPath.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Passes/StandardPasses.h"
#include "Renderer/Graph/RaytracingExecutionContext.h"
#include <imgui.h>

namespace Chimera
{
    RayTracedRenderPath::RayTracedRenderPath(VulkanContext& context)
        : RenderPath(std::shared_ptr<VulkanContext>(&context, [](VulkanContext*){}))
    {
    }

    RayTracedRenderPath::~RayTracedRenderPath()
    {
        m_RenderGraph.reset();
    }

    VkSemaphore RayTracedRenderPath::Render(const RenderFrameInfo& frameInfo)
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

        if (scene)
        {
            struct RTData { RGResourceHandle output; };
            m_RenderGraph->AddPass<RTData>("RaytracePass",
                [&](RTData& data, RenderGraph::PassBuilder& builder)
                {
                    data.output = builder.WriteStorage(RS::FinalColor, VK_FORMAT_R16G16B16A16_SFLOAT);
                },
                [=](const RTData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd)
                {
                    RaytracingExecutionContext ctx(reg.graph, reg.pass, cmd);
                    
                    RaytracingPipelineDescription desc{};
                    desc.raygen_shader = "raytracing/raytrace.rgen";
                    desc.miss_shaders = { "raytracing/miss.rmiss" };
                    desc.hit_shaders = { { "raytracing/closesthit.rchit", "raytracing/shadow.rahit" } };
                    
                    // [FIX] Must bind pipeline before tracing rays
                    ctx.BindPipeline(desc);

                    int useAlpha = m_UseAlphaTest ? 1 : 0;
                    ctx.PushConstants(VK_SHADER_STAGE_RAYGEN_BIT_KHR, useAlpha);

                    ctx.TraceRays(reg.graph.GetWidth(), reg.graph.GetHeight());
                }
            );
        }

        // 2. Linearize Depth (For debug view)
        StandardPasses::AddLinearizeDepthPass(*m_RenderGraph);

        m_RenderGraph->Compile();
        return m_RenderGraph->Execute(frameInfo.commandBuffer);
    }

    void RayTracedRenderPath::OnImGui()
    {
        if (ImGui::TreeNodeEx("Ray Tracing Settings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Alpha Test for Shadows", &m_UseAlphaTest);
            ImGui::TreePop();
        }
    }
}
