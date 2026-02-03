#include "pch.h"
#include "RayTracedRenderPath.h"
#include "core/application/Application.h"
#include "core/Config.h" 
#include "rendering/pipelines/common/RenderPathUtils.h"
#include "rendering/pipelines/common/ShaderLibrary.h"
#include "gfx/utils/VulkanBarrier.h"
#include <imgui.h>

namespace Chimera
{
    inline uint32_t alignedSize(uint32_t value, uint32_t alignment)
    {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    RayTracedRenderPath::RayTracedRenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager, VkDescriptorSetLayout globalDescriptorSetLayout)
        : RenderPath(context, scene, resourceManager), m_GlobalDescriptorSetLayout(globalDescriptorSetLayout)
    {
    }

    RayTracedRenderPath::~RayTracedRenderPath()
    {
        auto device = m_Context->GetDevice();
        if (m_Pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, m_Pipeline, nullptr);
        if (m_PipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, m_PipelineLayout, nullptr);
        if (m_RTDescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, m_RTDescriptorPool, nullptr);
        if (m_RTDescriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, m_RTDescriptorSetLayout, nullptr);
    }

    void RayTracedRenderPath::Init()
    {
        m_LastExtent = m_Context->GetSwapChainExtent();
        OnRecreateResources(m_LastExtent.width, m_LastExtent.height);

        std::vector<VkDescriptorSetLayoutBinding> bindings;
        bindings.resize(5); 

        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

        bindings[3].binding = 3;
        bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        bindings[4].binding = 4;
        bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        vkCreateDescriptorSetLayout(m_Context->GetDevice(), &layoutInfo, nullptr, &m_RTDescriptorSetLayout);

        InitPass(m_Scene->GetTLAS(), m_StorageImage.get(), m_RTDescriptorSetLayout, m_GlobalDescriptorSetLayout);
    }

    void RayTracedRenderPath::OnSceneUpdated()
    {
        UpdateDescriptorSets();
    }

    void RayTracedRenderPath::OnRecreateResources(uint32_t width, uint32_t height)
    {
        m_StorageImage = std::make_unique<Image>(
            m_Context->GetAllocator(), m_Context->GetDevice(),
            width, height,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT
        );
        
        // 使用阻塞版本 (Context) 进行初始化时的 Layout 转换
        VulkanUtils::TransitionImageLayout(m_Context, m_StorageImage->GetImage(), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        m_StorageImageLayout = VK_IMAGE_LAYOUT_GENERAL;

        if (m_RTDescriptorSets.size() > 0) UpdateDescriptorSets();
    }

    void RayTracedRenderPath::InitPass(VkAccelerationStructureKHR tlas, Image* storageImage, VkDescriptorSetLayout descriptorSetLayout, VkDescriptorSetLayout globalDescriptorSetLayout)
    {
        m_PassTLAS = tlas;
        m_PassStorageImage = storageImage;

        std::vector<VkDescriptorPoolSize> poolSizes = {
            { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 10 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 10 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 20 }
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 10;
        vkCreateDescriptorPool(m_Context->GetDevice(), &poolInfo, nullptr, &m_RTDescriptorPool);

        std::vector<VkDescriptorSetLayout> layouts(3, descriptorSetLayout);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_RTDescriptorPool;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        allocInfo.pSetLayouts = layouts.data();

        m_RTDescriptorSets.resize(3);
        vkAllocateDescriptorSets(m_Context->GetDevice(), &allocInfo, m_RTDescriptorSets.data());

        UpdateDescriptorSets();
        CreatePipeline(descriptorSetLayout, globalDescriptorSetLayout);
        CreateShaderBindingTable();
    }

    void RayTracedRenderPath::UpdateDescriptorSets()
    {
        for (size_t i = 0; i < m_RTDescriptorSets.size(); i++)
        {
            std::vector<VkWriteDescriptorSet> writes;

            VkWriteDescriptorSetAccelerationStructureKHR asWrite{};
            asWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
            asWrite.accelerationStructureCount = 1;
            asWrite.pAccelerationStructures = &m_PassTLAS;

            VkWriteDescriptorSet asWriteSet{};
            asWriteSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            asWriteSet.pNext = &asWrite;
            asWriteSet.dstSet = m_RTDescriptorSets[i];
            asWriteSet.dstBinding = 0;
            asWriteSet.descriptorCount = 1;
            asWriteSet.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            writes.push_back(asWriteSet);

            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageView = m_StorageImage->GetImageView();
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkWriteDescriptorSet imgWrite{};
            imgWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            imgWrite.dstSet = m_RTDescriptorSets[i];
            imgWrite.dstBinding = 1;
            imgWrite.descriptorCount = 1;
            imgWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            imgWrite.pImageInfo = &imageInfo;
            writes.push_back(imgWrite);

            VkDescriptorBufferInfo vbInfo{};
            if (m_Scene->GetVertexCount() > 0 && m_Scene->GetVertexBuffer() != VK_NULL_HANDLE)
            {
                vbInfo.buffer = m_Scene->GetVertexBuffer();
                vbInfo.offset = 0;
                vbInfo.range = VK_WHOLE_SIZE;

                VkWriteDescriptorSet vertexWrite{};
                vertexWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                vertexWrite.dstSet = m_RTDescriptorSets[i];
                vertexWrite.dstBinding = 3;
                vertexWrite.descriptorCount = 1;
                vertexWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                vertexWrite.pBufferInfo = &vbInfo;
                writes.push_back(vertexWrite);
            }

            VkDescriptorBufferInfo ibInfo{};
            if (m_Scene->GetIndexCount() > 0 && m_Scene->GetIndexBuffer() != VK_NULL_HANDLE)
            {
                ibInfo.buffer = m_Scene->GetIndexBuffer();
                ibInfo.offset = 0;
                ibInfo.range = VK_WHOLE_SIZE;

                VkWriteDescriptorSet indexWrite{};
                indexWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                indexWrite.dstSet = m_RTDescriptorSets[i];
                indexWrite.dstBinding = 4;
                indexWrite.descriptorCount = 1;
                indexWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                indexWrite.pBufferInfo = &ibInfo;
                writes.push_back(indexWrite);
            }

            if (!writes.empty())
            {
                vkUpdateDescriptorSets(m_Context->GetDevice(), (uint32_t)writes.size(), writes.data(), 0, nullptr);
            }
        }
    }

    void RayTracedRenderPath::CreatePipeline(VkDescriptorSetLayout descriptorSetLayout, VkDescriptorSetLayout globalDescriptorSetLayout)
    {
        std::vector<VkDescriptorSetLayout> setLayouts = { globalDescriptorSetLayout, descriptorSetLayout };
        
        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR;
        pcRange.offset = 0;
        pcRange.size = sizeof(RayTracingPushConstants);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pcRange;

        vkCreatePipelineLayout(m_Context->GetDevice(), &layoutInfo, nullptr, &m_PipelineLayout);
    }

    void RayTracedRenderPath::CreateShaderBindingTable()
    {
    }

    void RayTracedRenderPath::OnImGui()
    {
        ImGui::Text("Ray Tracing Enabled");
    }

    void RayTracedRenderPath::Render(VkCommandBuffer cmd, uint32_t currentFrame, uint32_t imageIndex, 
                                     VkDescriptorSet globalDescriptorSet, const std::vector<VkImage>& swapChainImages,
                                     std::function<void(VkCommandBuffer)> uiDrawCallback)
    {
        EnsureResources(m_Context->GetSwapChainExtent().width, m_Context->GetSwapChainExtent().height);

        // 1. Transition Storage Image to General (for writing)
        VulkanUtils::TransitionImageLayout(cmd, m_StorageImage->GetImage(), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_Pipeline);
        
        std::vector<VkDescriptorSet> sets = { globalDescriptorSet, m_RTDescriptorSets[currentFrame] };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_PipelineLayout, 0, (uint32_t)sets.size(), sets.data(), 0, nullptr);

        RayTracingPushConstants pc{};
        pc.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        pc.lightPos = glm::vec3(5.0f, 5.0f, 5.0f);
        pc.frameCount = m_FrameCount++;
        vkCmdPushConstants(cmd, m_PipelineLayout, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR, 0, sizeof(pc), &pc);

        vkCmdTraceRaysKHR(cmd, &m_RaygenRegion, &m_MissRegion, &m_HitRegion, &m_CallRegion, m_Context->GetSwapChainExtent().width, m_Context->GetSwapChainExtent().height, 1);

        // 2. Prepare Swapchain for Transfer Dst
        VulkanUtils::TransitionImageLayout(cmd, swapChainImages[imageIndex], m_Context->GetSwapChainImageFormat(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        
        // 3. Prepare Storage for Transfer Src
        VulkanUtils::TransitionImageLayout(cmd, m_StorageImage->GetImage(), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        VkImageCopy copyRegion{};
        copyRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copyRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copyRegion.extent = { m_Context->GetSwapChainExtent().width, m_Context->GetSwapChainExtent().height, 1 };
        
        vkCmdCopyImage(cmd, m_StorageImage->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapChainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        // 4. Transition Storage back to General (for next frame)
        VulkanUtils::TransitionImageLayout(cmd, m_StorageImage->GetImage(), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
        
        // 5. Prepare Swapchain for UI or Present
        if (uiDrawCallback) {
            // Swapchain: Transfer Dst -> Color Attachment (UI)
            VulkanUtils::TransitionImageLayout(cmd, swapChainImages[imageIndex], m_Context->GetSwapChainImageFormat(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            
            uiDrawCallback(cmd);
            
            // Swapchain: Color Attachment -> Present
            VulkanUtils::TransitionImageLayout(cmd, swapChainImages[imageIndex], m_Context->GetSwapChainImageFormat(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        } else {
            // Swapchain: Transfer Dst -> Present
            VulkanUtils::TransitionImageLayout(cmd, swapChainImages[imageIndex], m_Context->GetSwapChainImageFormat(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        }
    }

    // [修复] 正确实现辅助函数，调用 m_Context
    VkCommandBuffer RayTracedRenderPath::BeginSingleTimeCommands()
    {
        // 确保 RenderPath 基类中有 protected m_Context
        return m_Context->BeginSingleTimeCommands();
    }

    void RayTracedRenderPath::EndSingleTimeCommands(VkCommandBuffer commandBuffer)
    {
        m_Context->EndSingleTimeCommands(commandBuffer);
    }

    VkTransformMatrixKHR RayTracedRenderPath::ToVkMatrix(glm::mat4 model)
    {
        glm::mat4 t = glm::transpose(model);
        VkTransformMatrixKHR out;
        memcpy(&out, &t, sizeof(out));
        return out;
    }
}