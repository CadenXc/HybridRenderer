#include "pch.h"
#include "DeferredLightingPass.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Core/Application.h"

namespace Chimera {

    DeferredLightingPass::DeferredLightingPass()
        : RenderGraphPass("Deferred Lighting Pass")
    {
    }

    void DeferredLightingPass::Setup(RenderGraph& graph)
    {
        bool rtSupported = Application::GetVulkanContext().IsRayTracingSupported();
        auto renderOutput = TransientResource::Image(RS::FINAL_COLOR, VK_FORMAT_B8G8R8A8_UNORM, 0xFFFFFFFF, { { 0.0f, 0.0f, 0.0f, 1.0f } });

        GraphicsPipelineDescription pipelineDesc{};
        pipelineDesc.name = "Deferred Lighting Pipeline";
        pipelineDesc.Multisample.Samples = VK_SAMPLE_COUNT_1_BIT;
        pipelineDesc.vertex_shader = "fullscreen.vert";
        pipelineDesc.fragment_shader = "deferred_lighting.frag";
        pipelineDesc.vertex_input_state = VertexInputState::Empty;
        pipelineDesc.Rasterization.Cull = CullMode::None;
        pipelineDesc.DepthStencil.DepthTest = false;
        pipelineDesc.DepthStencil.DepthWrite = false;

        std::vector<TransientResource> inputs = {
            TransientResource::Image(RS::ALBEDO, VK_FORMAT_R8G8B8A8_UNORM),
            TransientResource::Image(RS::NORMAL, VK_FORMAT_R16G16B16A16_SFLOAT),
            TransientResource::Image(RS::MATERIAL, VK_FORMAT_R8G8B8A8_UNORM),
            TransientResource::Image(RS::DEPTH, VK_FORMAT_D32_SFLOAT)
        };

        if (rtSupported) {
            inputs.push_back(TransientResource::Image(RS::SVGF_OUTPUT, VK_FORMAT_R16G16B16A16_SFLOAT));
            inputs.push_back(TransientResource::Image(RS::RT_REFLECTIONS, VK_FORMAT_R16G16B16A16_SFLOAT));
        } else {
            // Placeholder: Bind albedo to satisfy layout
            inputs.push_back(TransientResource::Image(RS::ALBEDO, VK_FORMAT_R8G8B8A8_UNORM));
            inputs.push_back(TransientResource::Image(RS::ALBEDO, VK_FORMAT_R8G8B8A8_UNORM));
        }

        graph.AddGraphicsPass(m_Name,
            inputs, 
            { renderOutput }, 
            { pipelineDesc }, 
            [](ExecuteGraphicsCallback execute) 
            {
                execute("Deferred Lighting Pipeline",
                    [](GraphicsExecutionContext& ctx) 
                    {
                        ctx.Draw(3, 1, 0, 0); // Fullscreen triangle
                    }
                );
            },
            "Deferred_Standard" 
        );
    }

}
