#include "pch.h"
#include "HybridRenderPath.h"
#include "Renderer/Backend/RenderContext.h"
#include "Renderer/Passes/GBufferPass.h"
#include "Renderer/Passes/DeferredLightingPass.h"
#include "Renderer/Passes/RTShadowAOPass.h"
#include "Renderer/Passes/SVGFPass.h"
#include "Renderer/Passes/SVGFAtrousPass.h"
#include "Renderer/Passes/BloomPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Utils/VulkanBarrier.h"
#include <imgui.h>

namespace Chimera
{
    HybridRenderPath::HybridRenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager, PipelineManager& pipelineManager, VkDescriptorSetLayout globalDescriptorSetLayout)
        : RenderPath(context, scene, resourceManager, pipelineManager), m_GlobalDescriptorSetLayout(globalDescriptorSetLayout)
    {
    }

    HybridRenderPath::~HybridRenderPath()
    {
        vkDeviceWaitIdle(m_Context->GetDevice());
    }

    void HybridRenderPath::Init()
    {
        RenderPath::Init();
    }

    void HybridRenderPath::Update()
    {
        RenderPath::Update();

        static glm::mat4 lastView = glm::mat4(1.0f);
        if (m_Scene && m_Scene->GetCamera().view != lastView)
        {
            m_FrameCount = 0;
            lastView = m_Scene->GetCamera().view;
            m_NeedsRebuild = true; // Only rebuild on actual camera movement
        }
        else if (m_Context->IsRayTracingSupported())
        {
            m_FrameCount++;
        }

        // Ensure history images match current viewport size
        uint32_t w = m_Width != 0 ? m_Width : m_Context->GetSwapChainExtent().width;
        uint32_t h = m_Height != 0 ? m_Height : m_Context->GetSwapChainExtent().height;

        static uint32_t lastW = 0, lastH = 0;
        if ((w != lastW || h != lastH) && w > 0 && h > 0)
        {
            CH_CORE_INFO("HybridRenderPath: Viewport resized to {}x{}, reallocating history.", w, h);
            ResizeHistoryImages(w, h);
            m_NeedsRebuild = true;
            lastW = w; lastH = h;
        }
    }

    void HybridRenderPath::ResizeHistoryImages(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0) return; // FIX: Don't allocate 0x0 images

        vkDeviceWaitIdle(m_Context->GetDevice());
        auto allocator = m_Context->GetAllocator();
        auto device = m_Context->GetDevice();

        // [STABILITY FIX] Defer destruction of old history buffers using raw pointers
        if (m_PrevNormal) {
            Image* r1 = m_PrevNormal.release();
            Image* r2 = m_PrevDepth.release();
            Image* r3 = m_ShadowAOHistory.release();
            Image* r4 = m_MomentsHistory.release();
            Image* r5 = m_IntegratedShadowAO[0].release();
            Image* r6 = m_IntegratedShadowAO[1].release();

            ResourceManager::SubmitResourceFree([r1, r2, r3, r4, r5, r6](){
                delete r1; delete r2; delete r3; delete r4; delete r5; delete r6;
            });
        }

        m_PrevNormal = std::make_unique<Image>(allocator, device, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        m_PrevDepth  = std::make_unique<Image>(allocator, device, width, height, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
        m_ShadowAOHistory = std::make_unique<Image>(allocator, device, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        m_MomentsHistory = std::make_unique<Image>(allocator, device, width, height, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        
        m_IntegratedShadowAO[0] = std::make_unique<Image>(allocator, device, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        m_IntegratedShadowAO[1] = std::make_unique<Image>(allocator, device, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

        // Explicitly tell the graph about the initial layout if it already exists
        if (m_RenderGraph) {
            m_RenderGraph->GetImageAccess(RS::PREV_NORMAL) = { VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT };
            m_RenderGraph->GetImageAccess(RS::PREV_DEPTH)  = { VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT };
            m_RenderGraph->GetImageAccess(RS::SHADOW_AO_HIST) = { VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT };
            m_RenderGraph->GetImageAccess(RS::MOMENTS_HIST)   = { VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT };
        }
    }

    void HybridRenderPath::SetupGraph(RenderGraph& graph)
    {
        bool rtSupported = m_Context->IsRayTracingSupported();

        // 0. Register External History Resources
        if (rtSupported && m_PrevNormal) {
            graph.RegisterExternalResource(RS::PREV_NORMAL, { (uint32_t)m_PrevNormal->GetExtent().width, (uint32_t)m_PrevNormal->GetExtent().height, m_PrevNormal->GetFormat() });
            graph.RegisterExternalResource(RS::PREV_DEPTH,  { (uint32_t)m_PrevDepth->GetExtent().width,  (uint32_t)m_PrevDepth->GetExtent().height,  m_PrevDepth->GetFormat()  });
            graph.RegisterExternalResource(RS::SHADOW_AO_HIST, { (uint32_t)m_ShadowAOHistory->GetExtent().width, (uint32_t)m_ShadowAOHistory->GetExtent().height, m_ShadowAOHistory->GetFormat() });
            graph.RegisterExternalResource(RS::MOMENTS_HIST,   { (uint32_t)m_MomentsHistory->GetExtent().width,   (uint32_t)m_MomentsHistory->GetExtent().height,   m_MomentsHistory->GetFormat()   });
            
            // Ping-pong for Atrous
            graph.RegisterExternalResource(RS::ATROUS_PING, { (uint32_t)m_IntegratedShadowAO[0]->GetExtent().width, (uint32_t)m_IntegratedShadowAO[0]->GetExtent().height, m_IntegratedShadowAO[0]->GetFormat() });
            graph.RegisterExternalResource(RS::ATROUS_PONG, { (uint32_t)m_IntegratedShadowAO[1]->GetExtent().width, (uint32_t)m_IntegratedShadowAO[1]->GetExtent().height, m_IntegratedShadowAO[1]->GetFormat() });

            graph.SetExternalResource(RS::PREV_NORMAL, m_PrevNormal->GetImage(), m_PrevNormal->GetImageView(), VK_IMAGE_LAYOUT_GENERAL, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
            graph.SetExternalResource(RS::PREV_DEPTH,  m_PrevDepth->GetImage(),  m_PrevDepth->GetImageView(),  VK_IMAGE_LAYOUT_GENERAL, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
            graph.SetExternalResource(RS::SHADOW_AO_HIST, m_ShadowAOHistory->GetImage(), m_ShadowAOHistory->GetImageView(), VK_IMAGE_LAYOUT_GENERAL, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
            graph.SetExternalResource(RS::MOMENTS_HIST,   m_MomentsHistory->GetImage(),   m_MomentsHistory->GetImageView(),   VK_IMAGE_LAYOUT_GENERAL, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
            
            graph.SetExternalResource(RS::ATROUS_PING, m_IntegratedShadowAO[0]->GetImage(), m_IntegratedShadowAO[0]->GetImageView(), VK_IMAGE_LAYOUT_GENERAL, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
            graph.SetExternalResource(RS::ATROUS_PONG, m_IntegratedShadowAO[1]->GetImage(), m_IntegratedShadowAO[1]->GetImageView(), VK_IMAGE_LAYOUT_GENERAL, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        }

        GBufferPass gbuffer(m_Scene);
        gbuffer.Setup(graph);

        if (rtSupported) {
            m_FrameCount++;
            RTShadowAOPass shadowAO(m_Scene, m_FrameCount);
            shadowAO.Setup(graph);

            SVGFPass svgf;
            svgf.Setup(graph);

            // A-Trous filtering stages
            SVGFAtrousPass atrous1("Atrous Pass 1", RS::SVGF_OUTPUT, RS::ATROUS_PONG, 1);
            atrous1.Setup(graph);
            SVGFAtrousPass atrous2("Atrous Pass 2", RS::ATROUS_PONG, RS::ATROUS_PING, 2);
            atrous2.Setup(graph);
            SVGFAtrousPass atrous3("Atrous Pass 3", RS::ATROUS_PING, RS::ATROUS_PONG, 4);
            atrous3.Setup(graph);
            SVGFAtrousPass atrous4("Atrous Pass 4", RS::ATROUS_PONG, RS::ATROUS_PING, 8);
            atrous4.Setup(graph);
            SVGFAtrousPass atrous5("Atrous Pass 5", RS::ATROUS_PING, RS::SVGF_OUTPUT, 16);
            atrous5.Setup(graph);
        }

        DeferredLightingPass lighting;
        lighting.Setup(graph);

        // Apply Bloom Post-processing with dynamic UI parameters
        BloomPass::Setup(graph, RS::FINAL_COLOR, m_BloomThreshold, m_BloomIntensity);

        graph.AddBlitPass("Final Blit", RS::FINAL_COLOR, RS::RENDER_OUTPUT);

        // History Update Pass
        if (rtSupported) {
            graph.AddBlitPass("History Update: Normal", RS::NORMAL, RS::PREV_NORMAL, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT);
            graph.AddBlitPass("History Update: Depth",  RS::DEPTH,  RS::PREV_DEPTH,  VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT);
            graph.AddBlitPass("History Update: SVGF",   RS::SVGF_OUTPUT, RS::SHADOW_AO_HIST, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT);
        }

        graph.Build();
    }

    void HybridRenderPath::OnImGui()
    {
        ImGui::Text("Hybrid Rendering Path (RenderGraph)");
        ImGui::Text("RT Accumulation Frames: %d", m_FrameCount);

        if (ImGui::CollapsingHeader("Post Processing", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::SliderFloat("Bloom Threshold", &m_BloomThreshold, 0.0f, 5.0f)) m_NeedsRebuild = true;
            if (ImGui::SliderFloat("Bloom Intensity", &m_BloomIntensity, 0.0f, 2.0f)) m_NeedsRebuild = true;
        }
    }

    void HybridRenderPath::Render(VkCommandBuffer cmd, uint32_t currentFrame, uint32_t imageIndex, 
                                  VkDescriptorSet globalDescriptorSet, const std::vector<VkImage>& swapChainImages,
                                  std::function<void(VkCommandBuffer)> uiDrawCallback)
    {
        VkImage swapchainImage = swapChainImages[imageIndex];
        VkExtent2D extent = m_Context->GetSwapChainExtent();

        // 1. Map the swapchain to RENDER_OUTPUT node in RenderGraph (for the final Blit)
        m_RenderGraph->SetExternalResource(RS::RENDER_OUTPUT, swapchainImage, m_Context->GetSwapChainImageViews()[imageIndex], 
            VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

        // 2. Execute RenderGraph (This will run G-Buffer, RT, Lighting, Bloom, then Blit to RENDER_OUTPUT)
        m_RenderGraph->Execute(cmd, currentFrame, imageIndex, uiDrawCallback);

        // 3. FINAL DEFENSIVE BARRIER: Ensure swapchain is in PRESENT_SRC_KHR
        VulkanUtils::InsertImageBarrier(cmd, swapchainImage, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0);
    }
}
