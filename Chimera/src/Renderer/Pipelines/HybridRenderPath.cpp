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
#include "Renderer/Passes/BloomPass.h"
#include "Renderer/Passes/TAAPass.h"
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

    HybridRenderPath::~HybridRenderPath()
    {
        m_RenderGraph.reset();
    }

    VkSemaphore HybridRenderPath::Render(const RenderFrameInfo& frameInfo)
    {
        if (m_NeedsResize)
        {
            if (!m_Context)
            {
                return VK_NULL_HANDLE;
            }

            // --- FIX: Sync GPU before destroying old RenderGraph resources ---
            vkDeviceWaitIdle(m_Context->GetDevice());

            CH_CORE_INFO("HybridRenderPath: Resizing RenderGraph to {}x{}", m_Width, m_Height);
            m_RenderGraph = std::make_unique<RenderGraph>(*m_Context, m_Width, m_Height);
            m_NeedsResize = false;
        }

        if (!m_RenderGraph)
        {
            CH_CORE_ERROR("HybridRenderPath: RenderGraph is null!");
            return VK_NULL_HANDLE;
        }
        
        auto scene = GetSceneShared();
        m_RenderGraph->Reset();

        // Inform Graph about history buffers
        ImageDescription colorDesc = { m_Width, m_Height, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT };
        
        const auto& shadowAccumImg = m_RenderGraph->GetImage("ShadowAccum");
        if (shadowAccumImg.handle != VK_NULL_HANDLE) 
        {
            m_RenderGraph->SetExternalResource("History_ShadowAccum", shadowAccumImg.handle, shadowAccumImg.view, 
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, colorDesc);
        }

        const auto& shadowMomentsImg = m_RenderGraph->GetImage("ShadowMoments");
        if (shadowMomentsImg.handle != VK_NULL_HANDLE) 
        {
            m_RenderGraph->SetExternalResource("History_ShadowMoments", shadowMomentsImg.handle, shadowMomentsImg.view, 
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, colorDesc);
        }

        const auto& reflAccumImg = m_RenderGraph->GetImage("ReflAccum");
        if (reflAccumImg.handle != VK_NULL_HANDLE) 
        {
            m_RenderGraph->SetExternalResource("History_ReflAccum", reflAccumImg.handle, reflAccumImg.view, 
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, colorDesc);
        }

        const auto& reflMomentsImg = m_RenderGraph->GetImage("ReflMoments");
        if (reflMomentsImg.handle != VK_NULL_HANDLE) 
        {
            m_RenderGraph->SetExternalResource("History_ReflMoments", reflMomentsImg.handle, reflMomentsImg.view, 
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, colorDesc);
        }

        const auto& giAccumImg = m_RenderGraph->GetImage("GIAccum");
        if (giAccumImg.handle != VK_NULL_HANDLE) 
        {
            m_RenderGraph->SetExternalResource("History_GIAccum", giAccumImg.handle, giAccumImg.view, 
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, colorDesc);
        }

        const auto& giMomentsImg = m_RenderGraph->GetImage("GIMoments");
        if (giMomentsImg.handle != VK_NULL_HANDLE) 
        {
            m_RenderGraph->SetExternalResource("History_GIMoments", giMomentsImg.handle, giMomentsImg.view, 
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, colorDesc);
        }

        const auto& taaImg = m_RenderGraph->GetImage("TAA");
        if (taaImg.handle != VK_NULL_HANDLE) 
        {
            m_RenderGraph->SetExternalResource("History_TAA", taaImg.handle, taaImg.view, 
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, colorDesc);
        }

        // [FIX 2.1] Register Swapchain as external resource. Renderer::BeginFrame already handled transition to COLOR_ATTACHMENT.
        VkImage swapchainImage = m_Context->GetSwapChainImages()[frameInfo.imageIndex];
        VkExtent2D extent = m_Context->GetSwapChainExtent();
        
        ImageDescription swapDesc = { extent.width, extent.height, m_Context->GetSwapChainImageFormat(), VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT };
        m_RenderGraph->SetExternalResource(RS::RENDER_OUTPUT, swapchainImage, m_Context->GetSwapchain()->GetImageViews()[frameInfo.imageIndex],
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, swapDesc);

        // 1. G-Buffer
        if (scene) 
        {
            GBufferPass::AddToGraph(*m_RenderGraph, scene);
        }

        // Ray Tracing Passes (Only if TLAS exists)
        bool hasTLAS = scene && scene->GetTLAS() != VK_NULL_HANDLE;

        // 2. RT Shadow/AO
        if (hasTLAS) 
        {
            RTShadowAOPass::AddToGraph(*m_RenderGraph, scene);
        }

        // 3. RT Reflections
        if (hasTLAS) 
        {
            RTReflectionPass::AddToGraph(*m_RenderGraph, scene);
        }

        // 4. RT Diffuse GI
        if (hasTLAS) 
        {
            RTDiffuseGIPass::AddToGraph(*m_RenderGraph, scene);
        }

        // 5. SVGF Denoising (Shadows/AO)
        if (scene) 
        {
            SVGFPass::AddToGraph(*m_RenderGraph, scene, "CurColor", "Shadow", "ShadowAccum");
        }

        // 6. SVGF Denoising (Reflections)
        if (scene) 
        {
            SVGFPass::AddToGraph(*m_RenderGraph, scene, "ReflectionRaw", "Refl", "ReflAccum");
        }

        // 7. SVGF Denoising (Diffuse GI)
        if (scene) 
        {
            SVGFPass::AddToGraph(*m_RenderGraph, scene, "GIRaw", "GI", "GIAccum");
        }

        // 8. Composition
        struct CompData 
        { 
            RGResourceHandle albedo; 
            RGResourceHandle shadowFiltered; 
            RGResourceHandle reflectionFiltered; 
            RGResourceHandle giFiltered; 
            RGResourceHandle material; 
            RGResourceHandle normal; 
            RGResourceHandle depth; 
            RGResourceHandle output; 
        };

        m_RenderGraph->AddPass<CompData>("Composition",
            [&](CompData& data, RenderGraph::PassBuilder& builder) 
            {
                data.albedo             = builder.Read(RS::Albedo);
                data.shadowFiltered     = builder.Read("Shadow_Filtered_4");
                data.reflectionFiltered = builder.Read("Refl_Filtered_4");
                data.giFiltered         = builder.Read("GI_Filtered_4");
                data.material           = builder.Read(RS::Material);
                data.normal             = builder.Read(RS::Normal);
                data.depth              = builder.Read(RS::Depth);
                data.output             = builder.Write(RS::FinalColor, VK_FORMAT_R16G16B16A16_SFLOAT);
            },
            [=](const CompData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd) 
            {
                GraphicsExecutionContext ctx(reg.graph, reg.pass, cmd);
                
                int skyboxIndex = GetScene() ? GetScene()->GetSkyboxTextureIndex() : -1;
                ctx.BindPipeline({ "Composition", "common/fullscreen.vert", "postprocess/composition.frag" });
                ctx.PushConstants(VK_SHADER_STAGE_ALL, skyboxIndex);

                ctx.DrawMeshes({ "Composition", "common/fullscreen.vert", "postprocess/composition.frag" }, nullptr);
            }
        );

        // 9. Bloom
        BloomPass::AddToGraph(*m_RenderGraph);

        // 10. TAA
        TAAPass::AddToGraph(*m_RenderGraph);

        // 11. Final Output
        struct FinalData { RGResourceHandle src; };
        m_RenderGraph->AddPass<FinalData>("FinalBlit",
            [&](FinalData& data, RenderGraph::PassBuilder& builder) 
            {
                data.src = builder.Read("TAAOutput");
                builder.Write(RS::RENDER_OUTPUT);
            },
            [=](const FinalData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd) 
            {
                GraphicsExecutionContext ctx(reg.graph, reg.pass, cmd);
                ctx.DrawMeshes({ "FinalBlit", "common/fullscreen.vert", "postprocess/blit.frag", false, false }, nullptr);
            }
        );

        m_RenderGraph->Compile();
        m_RenderGraph->Execute(frameInfo.commandBuffer);

        return VK_NULL_HANDLE;
    }
}
