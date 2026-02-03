#include "pch.h"
#include "HybridRenderPath.h"
#include "rendering/pipelines/common/ShaderLibrary.h"
#include "rendering/pipelines/common/RenderPathUtils.h"
#include "rendering/graph/GraphicsExecutionContext.h"
#include <imgui.h>

namespace Chimera
{
    struct HybridPushConstants
    {
        glm::mat4 model;
        glm::mat4 normalMatrix;
        int materialIndex;
    };

    HybridRenderPath::HybridRenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager, VkDescriptorSetLayout globalDescriptorSetLayout)
        : RenderPath(context, scene, resourceManager), m_GlobalDescriptorSetLayout(globalDescriptorSetLayout)
    {
    }

    HybridRenderPath::~HybridRenderPath()
    {
        auto device = m_Context->GetDevice();
        if (m_GBufferPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, m_GBufferPipeline, nullptr);
        if (m_GBufferPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, m_GBufferPipelineLayout, nullptr);
    }

    void HybridRenderPath::Init()
    {
        m_LastExtent = m_Context->GetSwapChainExtent();
        CreateGBufferResources();
        SetupRenderGraph(m_LastExtent.width, m_LastExtent.height);
        CH_CORE_INFO("HybridRenderPath Initialized: RenderGraph Built");
    }

    void HybridRenderPath::OnRecreateResources(uint32_t width, uint32_t height)
    {
        m_AlbedoImage.reset();
        m_NormalImage.reset();
        m_MaterialImage.reset();
        m_MotionImage.reset();
        m_DepthImage.reset();
        CreateGBufferResources();
        
        SetupRenderGraph(width, height);
    }

    void HybridRenderPath::SetupRenderGraph(uint32_t width, uint32_t height)
    {
        m_RenderGraph = std::make_unique<RenderGraph>(*m_Context, *m_ResourceManager);

        // --- 1. G-Buffer Pass ---
        
        // RENDER_OUTPUT (Swapchain) - Binding 0
        TransientResource renderOutput = { TransientResourceType::Image, "RENDER_OUTPUT", { TransientImageType::AttachmentImage, 0, 0, m_Context->GetSwapChainImageFormat(), 0 } };
        renderOutput.image.clear_value.color = { { 0.1f, 0.1f, 0.1f, 1.0f } };

        // Normal - Binding 1
        ImageDescription normalDesc = { width, height, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_SAMPLE_COUNT_1_BIT };
        m_RenderGraph->RegisterExternalResource("Normal", normalDesc);
        m_RenderGraph->SetExternalResource("Normal", m_NormalImage->GetImage(), m_NormalImage->GetImageView(), VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        TransientResource normalOut = { TransientResourceType::Image, "Normal", { TransientImageType::AttachmentImage, 0, 0, VK_FORMAT_R16G16B16A16_SFLOAT, 1 } };
        normalOut.image.clear_value.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

        // Material - Binding 2
        ImageDescription matDesc = { width, height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_SAMPLE_COUNT_1_BIT };
        m_RenderGraph->RegisterExternalResource("Material", matDesc);
        m_RenderGraph->SetExternalResource("Material", m_MaterialImage->GetImage(), m_MaterialImage->GetImageView(), VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        TransientResource materialOut = { TransientResourceType::Image, "Material", { TransientImageType::AttachmentImage, 0, 0, VK_FORMAT_R8G8B8A8_UNORM, 2 } };
        materialOut.image.clear_value.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

        // Motion - Binding 3
        ImageDescription motionDesc = { width, height, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_SAMPLE_COUNT_1_BIT };
        m_RenderGraph->RegisterExternalResource("Motion", motionDesc);
        m_RenderGraph->SetExternalResource("Motion", m_MotionImage->GetImage(), m_MotionImage->GetImageView(), VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        TransientResource motionOut = { TransientResourceType::Image, "Motion", { TransientImageType::AttachmentImage, 0, 0, VK_FORMAT_R16G16_SFLOAT, 3 } };
        motionOut.image.clear_value.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

        // Depth - Binding 4
        ImageDescription depthDesc = { width, height, FindDepthFormat(), VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_SAMPLE_COUNT_1_BIT };
        m_RenderGraph->RegisterExternalResource("Depth", depthDesc);
        m_RenderGraph->SetExternalResource("Depth", m_DepthImage->GetImage(), m_DepthImage->GetImageView(), VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        TransientResource depthOut = { TransientResourceType::Image, "Depth", { TransientImageType::AttachmentImage, 0, 0, FindDepthFormat(), 4 } };
        depthOut.image.clear_value.depthStencil = { 1.0f, 0 };

        GraphicsPipelineDescription gbufferPipelineDesc{};
        gbufferPipelineDesc.name = "G-Buffer Pipeline";
        gbufferPipelineDesc.vertex_shader = "gbuffer.vert"; 
        gbufferPipelineDesc.fragment_shader = "gbuffer.frag";
        gbufferPipelineDesc.push_constants.size = sizeof(HybridPushConstants);
        gbufferPipelineDesc.push_constants.shader_stage = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        gbufferPipelineDesc.rasterization_state = RasterizationState::CullNone;
        gbufferPipelineDesc.dynamic_state = DynamicState::ViewportScissor;
        
        m_RenderGraph->AddGraphicsPass("G-Buffer Pass",
            {}, 
            { renderOutput, normalOut, materialOut, motionOut, depthOut }, 
            { gbufferPipelineDesc }, 
            [this](ExecuteGraphicsCallback execute) 
            {
                execute("G-Buffer Pipeline",
                    [this](GraphicsExecutionContext& ctx) 
                    {
                        VkCommandBuffer cmd = ctx.GetCommandBuffer();
                        
                        VkBuffer vertexBuffers[] = { m_Scene->GetVertexBuffer() };
                        VkDeviceSize offsets[] = { 0 };
                        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
                        vkCmdBindIndexBuffer(cmd, m_Scene->GetIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

                        // Dynamic Viewport & Scissor
                        VkViewport viewport{};
                        viewport.x = 0.0f;
                        viewport.y = 0.0f;
                        viewport.width = (float)m_Context->GetSwapChainExtent().width;
                        viewport.height = (float)m_Context->GetSwapChainExtent().height;
                        viewport.minDepth = 0.0f;
                        viewport.maxDepth = 1.0f;
                        ctx.SetViewport(viewport);

                        VkRect2D scissor{};
                        scissor.offset = { 0, 0 };
                        scissor.extent = m_Context->GetSwapChainExtent();
                        ctx.SetScissor(scissor);

                        const auto& meshes = m_Scene->GetMeshes();
                        
                        for (const auto& mesh : meshes)
                        {
                            HybridPushConstants pc{};
                            pc.model = mesh.transform;
                            pc.normalMatrix = glm::transpose(glm::inverse(mesh.transform));
                            pc.materialIndex = mesh.materialIndex;
                            
                            vkCmdPushConstants(cmd, ctx.GetPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(HybridPushConstants), &pc);
                            
                            vkCmdDrawIndexed(cmd, mesh.indexCount, 1, mesh.indexOffset, mesh.vertexOffset, 0);
                        }
                    }
                );
            }
        );

        m_RenderGraph->Build();
    }

    void HybridRenderPath::OnImGui()
    {
        ImGui::Text("Hybrid Rendering Enabled");
    }

    void HybridRenderPath::Render(VkCommandBuffer cmd, uint32_t currentFrame, uint32_t imageIndex, 
                                  VkDescriptorSet globalDescriptorSet, const std::vector<VkImage>& swapChainImages,
                                  std::function<void(VkCommandBuffer)> uiDrawCallback)
    {
        EnsureResources(m_Context->GetSwapChainExtent().width, m_Context->GetSwapChainExtent().height);

        m_RenderGraph->Execute(cmd, currentFrame, imageIndex);

        // RenderGraph puts RENDER_OUTPUT (Swapchain) into PRESENT_SRC_KHR.
        // We need to transition it to COLOR_ATTACHMENT_OPTIMAL to draw UI on top.

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = swapChainImages[imageIndex];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        if (uiDrawCallback)
        {
            barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // From RenderGraph
            barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.srcAccessMask = 0; 
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

            uiDrawCallback(cmd);

            barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask = 0;

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }
    }

    void HybridRenderPath::CreateGBufferResources()
    {
        uint32_t width = m_Context->GetSwapChainExtent().width;
        uint32_t height = m_Context->GetSwapChainExtent().height;

        // Note: m_AlbedoImage unused if writing directly to RENDER_OUTPUT, but kept for structure or future use
        m_AlbedoImage = std::make_unique<Image>(m_Context->GetAllocator(), m_Context->GetDevice(), width, height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        
        m_NormalImage = std::make_unique<Image>(m_Context->GetAllocator(), m_Context->GetDevice(), width, height, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        m_MaterialImage = std::make_unique<Image>(m_Context->GetAllocator(), m_Context->GetDevice(), width, height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        m_MotionImage = std::make_unique<Image>(m_Context->GetAllocator(), m_Context->GetDevice(), width, height, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        
        m_DepthImage = std::make_unique<Image>(m_Context->GetAllocator(), m_Context->GetDevice(), width, height, FindDepthFormat(), VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    }

    void HybridRenderPath::CreateGBufferPipeline()
    {
    }

    VkFormat HybridRenderPath::FindDepthFormat()
    {
        return m_Context->findSupportedFormat(
            { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
            VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
        );
    }
}