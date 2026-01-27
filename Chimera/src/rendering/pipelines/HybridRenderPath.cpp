#include "pch.h"
#include "HybridRenderPath.h"
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include "gfx/pipeline/Pipeline.h"
#include "gfx/utils/VulkanBarrier.h"
#include "gfx/utils/VulkanShaderUtils.h"
#include "gfx/utils/VulkanDescriptorUtils.h"
#include "rendering/pipelines/common/RenderPathUtils.h"
#include "rendering/pipelines/common/ShaderLibrary.h"

namespace Chimera {

    HybridRenderPath::HybridRenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager, VkDescriptorSetLayout globalDescriptorSetLayout)
        : RenderPath(context, scene, resourceManager), m_GlobalDescriptorSetLayout(globalDescriptorSetLayout)
    {
    }

    HybridRenderPath::~HybridRenderPath()
    {
        auto device = m_Context->GetDevice();
        
        if (m_SimplePipeline.handle != VK_NULL_HANDLE) vkDestroyPipeline(device, m_SimplePipeline.handle, nullptr);
        if (m_SimplePipeline.layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, m_SimplePipeline.layout, nullptr);
        
        if (m_LightingPipeline.handle != VK_NULL_HANDLE) vkDestroyPipeline(device, m_LightingPipeline.handle, nullptr);
        if (m_LightingPipeline.layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, m_LightingPipeline.layout, nullptr);

        if (m_LightingDescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, m_LightingDescriptorPool, nullptr);
        }
        if (m_LightingDescriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, m_LightingDescriptorSetLayout, nullptr);
        }
    }

    void HybridRenderPath::Init()
    {
        // 1. G-Buffer Pipeline
        GraphicsPipelineDescription desc = {};
        desc.name = "G-Buffer Pass";
        desc.vertex_shader = "gbuffer.vert";
        desc.fragment_shader = "gbuffer.frag";
        desc.vertex_input_state = VertexInputState::Default;
        desc.multisample_state = MultisampleState::Off;
        desc.depth_stencil_state = DepthStencilState::On;
        desc.dynamic_state = DynamicState::ViewportScissor;
        desc.push_constants = PUSHCONSTANTS_NONE;

        GraphicsPass graphicsPass = {};
        graphicsPass.handle = VK_NULL_HANDLE; 
        TransientResource albedoAtt = { TransientResourceType::Image, "Albedo" };
        albedoAtt.image.format = VK_FORMAT_R8G8B8A8_UNORM;
        TransientResource normalAtt = { TransientResourceType::Image, "Normal" };
        normalAtt.image.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        TransientResource matAtt = { TransientResourceType::Image, "Material" };
        matAtt.image.format = VK_FORMAT_R8G8B8A8_UNORM;
        TransientResource depthAtt = { TransientResourceType::Image, "Depth" };
        depthAtt.image.format = VK_FORMAT_D32_SFLOAT;
        graphicsPass.attachments = { albedoAtt, normalAtt, matAtt, depthAtt };

        RenderPass renderPass = {};
        renderPass.pass = graphicsPass;
        renderPass.descriptor_set_layout = VK_NULL_HANDLE;
        m_SimplePipeline = VulkanUtils::CreateGraphicsPipeline(m_Context, *m_ResourceManager, renderPass, desc);

        // 2. Lighting Compute Pipeline
        VkDescriptorSetLayoutBinding bindings[4];
        bindings[0] = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        bindings[1] = { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        bindings[2] = { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        bindings[3] = { 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        
        VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 4, bindings };
        vkCreateDescriptorSetLayout(m_Context->GetDevice(), &layoutInfo, nullptr, &m_LightingDescriptorSetLayout);

        RenderPass compRenderPass = {};
        compRenderPass.descriptor_set_layout = m_LightingDescriptorSetLayout;
        ComputeKernel kernel = { "hybrid_lighting.comp" };
        m_LightingPipeline = VulkanUtils::CreateComputePipeline(m_Context, *m_ResourceManager, compRenderPass, PUSHCONSTANTS_NONE, kernel);

        CH_CORE_INFO("HybridRenderPath Initialized: Pipelines Created");
    }

    void HybridRenderPath::Resize(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0) return;
        auto device = m_Context->GetDevice();
        auto allocator = m_Context->GetAllocator();

        m_DepthImage.reset(); m_AlbedoImage.reset(); m_NormalImage.reset(); m_MaterialImage.reset(); m_LightingResultImage.reset();
        m_ImGuiTextureSets.clear();

        m_DepthImage = std::make_unique<Image>(allocator, device, width, height, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
        m_AlbedoImage = std::make_unique<Image>(allocator, device, width, height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        m_NormalImage = std::make_unique<Image>(allocator, device, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        m_MaterialImage = std::make_unique<Image>(allocator, device, width, height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        m_LightingResultImage = std::make_unique<Image>(allocator, device, width, height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        
        RenderPathUtils::TransitionImageLayout(m_Context, m_DepthImage->GetImage(), VK_FORMAT_D32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        RenderPathUtils::TransitionImageLayout(m_Context, m_AlbedoImage->GetImage(), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        RenderPathUtils::TransitionImageLayout(m_Context, m_NormalImage->GetImage(), VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        RenderPathUtils::TransitionImageLayout(m_Context, m_MaterialImage->GetImage(), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        RenderPathUtils::TransitionImageLayout(m_Context, m_LightingResultImage->GetImage(), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

        // Descriptors
        if (m_LightingDescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, m_LightingDescriptorPool, nullptr);
        VkDescriptorPoolSize ps[2] = { {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 9}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3} };
        VkDescriptorPoolCreateInfo pInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 3, 2, ps };
        vkCreateDescriptorPool(device, &pInfo, nullptr, &m_LightingDescriptorPool);

        std::vector<VkDescriptorSetLayout> layouts(3, m_LightingDescriptorSetLayout);
        VkDescriptorSetAllocateInfo aInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, m_LightingDescriptorPool, 3, layouts.data() };
        m_LightingDescriptorSets.resize(3);
        vkAllocateDescriptorSets(device, &aInfo, m_LightingDescriptorSets.data());

        for (int i = 0; i < 3; i++) {
            VkDescriptorImageInfo di[3] = { {m_ResourceManager->GetDefaultSampler(), m_AlbedoImage->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, {m_ResourceManager->GetDefaultSampler(), m_NormalImage->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, {m_ResourceManager->GetDefaultSampler(), m_DepthImage->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL} };
            VkDescriptorImageInfo outInfo{ VK_NULL_HANDLE, m_LightingResultImage->GetView(), VK_IMAGE_LAYOUT_GENERAL };
            VkWriteDescriptorSet w[4];
            for(int j=0; j<3; ++j) w[j] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_LightingDescriptorSets[i], (uint32_t)j, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &di[j] };
            w[3] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_LightingDescriptorSets[i], 3, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &outInfo };
            vkUpdateDescriptorSets(device, 4, w, 0, nullptr);
        }

        m_ImGuiTextureSets.push_back(ImGui_ImplVulkan_AddTexture(m_ResourceManager->GetDefaultSampler(), m_AlbedoImage->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
        m_ImGuiTextureSets.push_back(ImGui_ImplVulkan_AddTexture(m_ResourceManager->GetDefaultSampler(), m_NormalImage->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
        m_ImGuiTextureSets.push_back(ImGui_ImplVulkan_AddTexture(m_ResourceManager->GetDefaultSampler(), m_LightingResultImage->GetView(), VK_IMAGE_LAYOUT_GENERAL));
    }

    void HybridRenderPath::Render(VkCommandBuffer cmd, uint32_t currentFrame, uint32_t imageIndex, VkDescriptorSet globalDescriptorSet, const std::vector<VkImage>& swapChainImages, std::function<void(VkCommandBuffer)> uiDrawCallback)
    {
        VkExtent2D extent = m_Context->GetSwapChainExtent();
        if (!m_DepthImage || m_DepthImage->GetExtent().width != extent.width) Resize(extent.width, extent.height);

        // 1. G-Buffer Pass
        {
            VkImageMemoryBarrier b[3];
            for(int i=0; i<3; ++i) b[i] = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, 0, VK_NULL_HANDLE, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1} };
            b[0].image = m_AlbedoImage->GetImage(); b[1].image = m_NormalImage->GetImage(); b[2].image = m_MaterialImage->GetImage();
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 3, b);
        }

        VkRenderingAttachmentInfo cats[3];
        for(int i=0; i<3; ++i) cats[i] = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, nullptr, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_RESOLVE_MODE_NONE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, {0,0,0,1} };
        cats[0].imageView = m_AlbedoImage->GetView(); cats[1].imageView = m_NormalImage->GetView(); cats[2].imageView = m_MaterialImage->GetView();
        VkRenderingAttachmentInfo dats{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, nullptr, m_DepthImage->GetView(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_RESOLVE_MODE_NONE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, {1,0} };
        VkRenderingInfo rInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO, nullptr, 0, {{0,0}, extent}, 1, 0, 3, cats, &dats, nullptr };
        
        vkCmdBeginRendering(cmd, &rInfo);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SimplePipeline.handle);
        VkViewport vp{0,0,(float)extent.width,(float)extent.height,0,1}; vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D sc{{0,0}, extent}; vkCmdSetScissor(cmd, 0, 1, &sc);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SimplePipeline.layout, 0, 1, &globalDescriptorSet, 0, nullptr);
        if (m_Scene->GetVertexCount() > 0) {
            VkBuffer vbs[] = { m_Scene->GetVertexBuffer()->GetBuffer() }; VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, vbs, offsets); vkCmdBindIndexBuffer(cmd, m_Scene->GetIndexBuffer()->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, m_Scene->GetIndexCount(), 1, 0, 0, 0);
        }
        vkCmdEndRendering(cmd);

        // 2. Transition G-Buffer to Read
        {
            VkImageMemoryBarrier b[3];
            for(int i=0; i<3; ++i) b[i] = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 0, VK_NULL_HANDLE, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1} };
            b[0].image = m_AlbedoImage->GetImage(); b[1].image = m_NormalImage->GetImage(); b[2].image = m_MaterialImage->GetImage();
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 3, b);
            VkImageMemoryBarrier db = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 0, VK_NULL_HANDLE, {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1} };
            db.image = m_DepthImage->GetImage();
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &db);
        }

        // 3. Lighting Compute Pass
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_LightingPipeline.handle);
        VkDescriptorSet sets[] = { m_LightingDescriptorSets[currentFrame], globalDescriptorSet };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_LightingPipeline.layout, 0, 2, sets, 0, nullptr);
        vkCmdDispatch(cmd, (extent.width + 15) / 16, (extent.height + 15) / 16, 1);

        // 4. Blit Result to Swapchain
        {
            VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0, 0, VK_NULL_HANDLE, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1} };
            b.image = m_LightingResultImage->GetImage();
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
            VkImageMemoryBarrier sb = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, 0, VK_NULL_HANDLE, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1} };
            sb.image = swapChainImages[imageIndex];
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &sb);
        }
        
        RenderPathUtils::BlitToSwapchain(cmd, m_Context, m_LightingResultImage->GetImage(), swapChainImages[imageIndex], extent);

        // Final Transitions
        {
            VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, 0, 0, VK_NULL_HANDLE, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1} };
            b.image = m_LightingResultImage->GetImage();
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
            VkImageMemoryBarrier db = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 0, 0, VK_NULL_HANDLE, {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1} };
            db.image = m_DepthImage->GetImage();
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, nullptr, 0, nullptr, 1, &db);
        }

        if (uiDrawCallback) {
            VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, 0, VK_NULL_HANDLE, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1} };
            b.image = swapChainImages[imageIndex];
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
            uiDrawCallback(cmd);
        } else {
            VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 0, 0, VK_NULL_HANDLE, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1} };
            b.image = swapChainImages[imageIndex];
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        }
    }

    void HybridRenderPath::OnImGui() {
        ImGui::Begin("Hybrid Render Settings");
        if (ImGui::CollapsingHeader("G-Buffer Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
            static const char* n[] = { "Albedo", "Normal", "Lighting Result" };
            for (size_t i = 0; i < m_ImGuiTextureSets.size(); i++) {
                ImGui::Text("%s", n[i]);
                float aspect = (float)m_LightingResultImage->GetExtent().height / m_LightingResultImage->GetExtent().width;
                ImGui::Image((ImTextureID)m_ImGuiTextureSets[i], ImVec2(200, 200 * aspect));
                if (i < m_ImGuiTextureSets.size() - 1) ImGui::SameLine();
            }
        }
        ImGui::End();
    }

    void HybridRenderPath::OnSceneUpdated() {}
}




