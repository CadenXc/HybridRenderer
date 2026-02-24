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

    HybridRenderPath::~HybridRenderPath()
    {
        m_RenderGraph.reset();
    }

    VkSemaphore HybridRenderPath::Render(const RenderFrameInfo& frameInfo)
    {
        if (m_NeedsResize)
        {
            if (!m_Context) return VK_NULL_HANDLE;
            CH_CORE_INFO("HybridRenderPath: Resizing RenderGraph to {}x{}", m_Width, m_Height);
            m_RenderGraph = std::make_unique<RenderGraph>(*m_Context, m_Width, m_Height);
            m_NeedsResize = false;
        }

        if (!m_RenderGraph) return VK_NULL_HANDLE;
        
        auto scene = GetSceneShared();
        m_RenderGraph->Reset();

        // 1. Inform Graph about history buffers (Persistent images from pool)
        ImageDescription colorDesc = { m_Width, m_Height, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT };
        
        auto registerHistory = [&](const std::string& name, const std::string& externalName) 
        {
            const auto& img = m_RenderGraph->GetImage(name);
            if (img.handle != VK_NULL_HANDLE) 
            {
                m_RenderGraph->SetExternalResource(externalName, img.handle, img.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, colorDesc);
            }
        };

        registerHistory("ShadowAccum", "History_ShadowAccum");
        registerHistory("ShadowMoments", "History_ShadowMoments");
        registerHistory("ReflAccum", "History_ReflAccum");
        registerHistory("ReflMoments", "History_ReflMoments");
        registerHistory("GIAccum", "History_GIAccum");
        registerHistory("GIMoments", "History_GIMoments");
        registerHistory("TAA", "History_TAA");

        // 2. Main Passes
        if (scene) GBufferPass::AddToGraph(*m_RenderGraph, scene);

        bool hasTLAS = scene && scene->GetTLAS() != VK_NULL_HANDLE;
        if (hasTLAS) 
        {
            RTShadowAOPass::AddToGraph(*m_RenderGraph, scene);
            RTReflectionPass::AddToGraph(*m_RenderGraph, scene);
            RTDiffuseGIPass::AddToGraph(*m_RenderGraph, scene);
        }

        if (scene) 
        {
            SVGFPass::AddToGraph(*m_RenderGraph, scene, "CurColor", "Shadow", "ShadowAccum");
            SVGFPass::AddToGraph(*m_RenderGraph, scene, "ReflectionRaw", "Refl", "ReflAccum");
            SVGFPass::AddToGraph(*m_RenderGraph, scene, "GIRaw", "GI", "GIAccum");
        }

        // 3. Composition
        struct CompData { 
            RGResourceHandle albedo, shadow, shadow_raw, reflection, reflection_raw, gi, gi_raw, material, normal, depth, output, motion; 
        };

        m_RenderGraph->AddPass<CompData>("Composition",
            [&](CompData& data, RenderGraph::PassBuilder& builder) 
            {
                data.albedo         = builder.Read(RS::Albedo);
                data.shadow         = builder.Read("Shadow_Filtered_4");
                data.shadow_raw     = builder.Read("Shadow");
                data.reflection     = builder.Read("Refl_Filtered_4");
                data.reflection_raw = builder.Read("ReflectionRaw");
                data.gi             = builder.Read("GI_Filtered_4");
                data.gi_raw         = builder.Read("GIRaw");
                data.material       = builder.Read(RS::Material);
                data.normal         = builder.Read(RS::Normal);
                data.depth          = builder.Read(RS::Depth);
                data.motion         = builder.Read(RS::Motion);
                data.output         = builder.Write(RS::FinalColor, VK_FORMAT_R16G16B16A16_SFLOAT);
            },
            [=](const CompData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd) 
            {
                GraphicsExecutionContext ctx(reg.graph, reg.pass, cmd);
                ctx.DrawMeshes({ "Composition", "common/fullscreen.vert", "postprocess/composition.frag" }, nullptr);
            }
        );

        // 4. Post-processing chain (Temporarily disabled for diagnosis)
        StandardPasses::AddLinearizeDepthPass(*m_RenderGraph);
        // BloomPass::AddToGraph(*m_RenderGraph);
        // TAAPass::AddToGraph(*m_RenderGraph);

        m_RenderGraph->Compile();
        m_RenderGraph->Execute(frameInfo.commandBuffer);

        return VK_NULL_HANDLE;
    }
}
