#include "pch.h"
#include "HybridRenderPath.h"
#include <imgui.h>

namespace Chimera {

    HybridRenderPath::HybridRenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager, VkDescriptorSetLayout globalDescriptorSetLayout)
        : RenderPath(context, scene, resourceManager), m_GlobalDescriptorSetLayout(globalDescriptorSetLayout)
    {
    }

    HybridRenderPath::~HybridRenderPath()
    {
    }

    void HybridRenderPath::Init()
    {
        // Placeholder initialization
    }

    void HybridRenderPath::OnResize(uint32_t width, uint32_t height)
    {
        // Placeholder resize
    }

    void HybridRenderPath::OnSceneUpdated()
    {
        // Placeholder scene update
    }

    void HybridRenderPath::OnImGui()
    {
        ImGui::Begin("Hybrid Render Settings");

        ImGui::Text("Shadow Mode:");
        ImGui::RadioButton("Raytraced Shadows", &m_ShadowMode, SHADOW_MODE_RAYTRACED);
        ImGui::RadioButton("Rasterized Shadows", &m_ShadowMode, SHADOW_MODE_RASTERIZED);
        ImGui::RadioButton("No Shadows", &m_ShadowMode, SHADOW_MODE_OFF);
        ImGui::NewLine();

        ImGui::Text("Ambient Occlusion Mode:");
        ImGui::RadioButton("Raytraced Ambient Occlusion", &m_AmbientOcclusionMode, AMBIENT_OCCLUSION_MODE_RAYTRACED);
        ImGui::RadioButton("Screen-Space Ambient Occlusion", &m_AmbientOcclusionMode, AMBIENT_OCCLUSION_MODE_SSAO);
        ImGui::RadioButton("No Ambient Occlusion", &m_AmbientOcclusionMode, AMBIENT_OCCLUSION_MODE_OFF);
        ImGui::NewLine();
        ImGui::Checkbox("Denoise Shadows and Ambient Occlusion", &m_DenoiseShadowAndAO);
        ImGui::NewLine();
        ImGui::NewLine();

        ImGui::Text("Reflection Mode:");
        ImGui::RadioButton("Raytraced Reflections", &m_ReflectionMode, REFLECTION_MODE_RAYTRACED);
        ImGui::RadioButton("Screen-Space Reflections", &m_ReflectionMode, REFLECTION_MODE_SSR);
        ImGui::RadioButton("No Reflections", &m_ReflectionMode, REFLECTION_MODE_OFF);
        ImGui::NewLine();
        ImGui::NewLine();

        if(m_AmbientOcclusionMode == AMBIENT_OCCLUSION_MODE_SSAO) {
            ImGui::Text("SSAO Settings");
            ImGui::SliderFloat("Radius", &m_SSAOSettings.radius, 0.1f, 5.0f);
        }

        if(m_ReflectionMode == REFLECTION_MODE_SSR) {
            ImGui::Text("SSR Settings");
            ImGui::SliderFloat("Ray Distance", &m_SSRSettings.ray_distance, 0.1f, 40.0f);
            ImGui::SliderFloat("Step Size", &m_SSRSettings.step_size, 0.01f, 5.0f);
            ImGui::SliderFloat("Thickness", &m_SSRSettings.thickness, 0.0, 3.0f);
            ImGui::SliderInt("Binary Search Steps", &m_SSRSettings.bsearch_steps, 1, 100);
        }

        ImGui::End();
    }

    void HybridRenderPath::Render(VkCommandBuffer cmd, uint32_t currentFrame, uint32_t imageIndex, 
                                  VkDescriptorSet globalDescriptorSet, const std::vector<VkImage>& swapChainImages,
                                  std::function<void(VkCommandBuffer)> uiDrawCallback)
    {
        // For now, just clear the screen and draw UI.
        
        // 1. Transition Swapchain Image to Color Attachment
        {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.image = swapChainImages[imageIndex];
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        // 2. Begin Dynamic Rendering (Clear Pass)
        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = m_Context->GetSwapChainImageViews()[imageIndex];
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue = { 0.1f, 0.1f, 0.1f, 1.0f }; // Dark grey background

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea = { {0, 0}, m_Context->GetSwapChainExtent() };
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;

        vkCmdBeginRendering(cmd, &renderingInfo);
        vkCmdEndRendering(cmd);

        // 3. Draw UI
        if (uiDrawCallback) {
            uiDrawCallback(cmd);
        }

        // 4. Transition Swapchain to Present
        {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask = 0;
            barrier.image = swapChainImages[imageIndex];
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }
    }

    VkCommandBuffer HybridRenderPath::BeginSingleTimeCommands()
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = m_Context->GetCommandPool();
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(m_Context->GetDevice(), &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        return commandBuffer;
    }

    void HybridRenderPath::EndSingleTimeCommands(VkCommandBuffer commandBuffer)
    {
        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        vkQueueSubmit(m_Context->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_Context->GetGraphicsQueue());

        vkFreeCommandBuffers(m_Context->GetDevice(), m_Context->GetCommandPool(), 1, &commandBuffer);
    }

    void HybridRenderPath::TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels)
    {
        VkCommandBuffer commandBuffer = BeginSingleTimeCommands();
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

        vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        EndSingleTimeCommands(commandBuffer);
    }

}
