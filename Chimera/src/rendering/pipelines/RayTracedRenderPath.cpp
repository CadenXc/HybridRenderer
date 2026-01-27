#include "pch.h"
#include "RayTracedRenderPath.h"
#include "core/application/Application.h"
#include "core/Config.h" 
#include "rendering/pipelines/common/RenderPathUtils.h"
#include "rendering/pipelines/common/ShaderLibrary.h"
#include "gfx/utils/VulkanBarrier.h"
#include <imgui.h>

namespace Chimera {

    inline uint32_t alignedSize(uint32_t value, uint32_t alignment) {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    RayTracedRenderPath::RayTracedRenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager, VkDescriptorSetLayout globalDescriptorSetLayout)
        : RenderPath(context, scene, resourceManager), m_GlobalDescriptorSetLayout(globalDescriptorSetLayout) {}

    RayTracedRenderPath::~RayTracedRenderPath() {
        auto device = m_Context->GetDevice();
        if (m_Pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, m_Pipeline, nullptr);
        if (m_PipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, m_PipelineLayout, nullptr);
        if (m_RTDescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, m_RTDescriptorPool, nullptr);
        if (m_RTDescriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, m_RTDescriptorSetLayout, nullptr);
    }

    void RayTracedRenderPath::Init() {
        CreateStorageImage();
        CreateAccumulationImage();
        CreateRayTracingDescriptorSetLayout();
        CreateRayTracingDescriptorSets();
        InitPass(m_Scene->GetTLAS(), m_StorageImage.get(), m_RTDescriptorSetLayout, m_GlobalDescriptorSetLayout);
    }
    
    void RayTracedRenderPath::Resize(uint32_t width, uint32_t height) {
        m_FrameCount = 0; 
        m_StorageImage.reset();
        m_AccumulationImage.reset();
        CreateStorageImage();
        CreateAccumulationImage();
        auto device = m_Context->GetDevice();
        for (size_t i = 0; i < m_RTDescriptorSets.size(); i++) {
            VkDescriptorImageInfo storageImageInfo{ VK_NULL_HANDLE, m_StorageImage->GetView(), VK_IMAGE_LAYOUT_GENERAL };
            VkDescriptorImageInfo accumImageInfo{ VK_NULL_HANDLE, m_AccumulationImage->GetView(), VK_IMAGE_LAYOUT_GENERAL };
            VkWriteDescriptorSet writes[2];
            writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_RTDescriptorSets[i], 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &storageImageInfo };
            writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_RTDescriptorSets[i], 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &accumImageInfo };
            vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
        }
        InitPass(m_Scene->GetTLAS(), m_StorageImage.get(), m_RTDescriptorSetLayout, m_GlobalDescriptorSetLayout);
    }

    void RayTracedRenderPath::InitPass(VkAccelerationStructureKHR tlas, Image* storageImage, VkDescriptorSetLayout descriptorSetLayout, VkDescriptorSetLayout globalDescriptorSetLayout) {
        m_PassTLAS = tlas;
        m_PassStorageImage = storageImage;
        CreatePipeline(descriptorSetLayout, globalDescriptorSetLayout);
        CreateShaderBindingTable();
    }

    void RayTracedRenderPath::OnSceneUpdated() {
        if (m_RTDescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(m_Context->GetDevice(), m_RTDescriptorPool, nullptr);
        m_RTDescriptorPool = VK_NULL_HANDLE;
        CreateRayTracingDescriptorSets();
        InitPass(m_Scene->GetTLAS(), m_StorageImage.get(), m_RTDescriptorSetLayout, m_GlobalDescriptorSetLayout);
    }

    void RayTracedRenderPath::OnImGui() {
        ImGui::Begin("Ray Traced Render Settings");
        ImGui::Text("Accumulated Frames: %d", m_FrameCount);
        ImGui::End();
    }

    void RayTracedRenderPath::Render(VkCommandBuffer cmd, uint32_t currentFrame, uint32_t imageIndex, VkDescriptorSet globalDescriptorSet, const std::vector<VkImage>& swapChainImages, std::function<void(VkCommandBuffer)> uiDrawCallback) {
        VkExtent2D extent = m_Context->GetSwapChainExtent();
        if (!m_StorageImage || m_StorageImage->GetExtent().width != extent.width || m_StorageImage->GetExtent().height != extent.height) {
            Resize(extent.width, extent.height);
        }

        if (m_StorageImageLayout != VK_IMAGE_LAYOUT_GENERAL) {
            VkImageMemoryBarrier barrier = VulkanUtils::CreateImageBarrier(m_StorageImage->GetImage(), m_StorageImageLayout, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT);
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            m_StorageImageLayout = VK_IMAGE_LAYOUT_GENERAL;
        }
        
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_Pipeline);
        VkDescriptorSet sets[] = { m_RTDescriptorSets[currentFrame], globalDescriptorSet };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_PipelineLayout, 0, 2, sets, 0, nullptr);

        RayTracingPushConstants pc{ {0,0,0,1}, {2,4,1}, 1.0f, (int)m_FrameCount++ };
        vkCmdPushConstants(cmd, m_PipelineLayout, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR, 0, sizeof(pc), &pc);
        vkCmdTraceRaysKHR(cmd, &m_RaygenRegion, &m_MissRegion, &m_HitRegion, &m_CallRegion, extent.width, extent.height, 1);

        VkImageMemoryBarrier barriers[2];
        barriers[0] = VulkanUtils::CreateImageBarrier(swapChainImages[imageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
        barriers[1] = VulkanUtils::CreateImageBarrier(m_StorageImage->GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers);

        VkImageCopy copyRegion{ {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0,0,0}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0,0,0}, {extent.width, extent.height, 1} };
        vkCmdCopyImage(cmd, m_StorageImage->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapChainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
        m_StorageImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

        if (uiDrawCallback) {
            VkImageMemoryBarrier b = VulkanUtils::CreateImageBarrier(swapChainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
            uiDrawCallback(cmd);
        } else {
            VkImageMemoryBarrier b = VulkanUtils::CreateImageBarrier(swapChainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_WRITE_BIT, 0);
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        }
    }

    void RayTracedRenderPath::CreatePipeline(VkDescriptorSetLayout descriptorSetLayout, VkDescriptorSetLayout globalDescriptorSetLayout) {
        auto device = m_Context->GetDevice();
        
        // Clean up old pipeline and layout if they exist
        if (m_Pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, m_Pipeline, nullptr);
            m_Pipeline = VK_NULL_HANDLE;
        }
        if (m_PipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, m_PipelineLayout, nullptr);
            m_PipelineLayout = VK_NULL_HANDLE;
        }

        VkPipelineShaderStageCreateInfo rgenStage = ShaderLibrary::CreateShaderStage(device, "raygen.rgen", VK_SHADER_STAGE_RAYGEN_BIT_KHR);
        VkPipelineShaderStageCreateInfo rmissStage = ShaderLibrary::CreateShaderStage(device, "miss.rmiss", VK_SHADER_STAGE_MISS_BIT_KHR);
        VkPipelineShaderStageCreateInfo rchitStage = ShaderLibrary::CreateShaderStage(device, "closesthit.rchit", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
        
        VkPipelineShaderStageCreateInfo stages[] = { rgenStage, rmissStage, rchitStage };

        VkRayTracingShaderGroupCreateInfoKHR groups[3];
        for(int i=0; i<3; ++i) { groups[i] = { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR, nullptr, VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR }; }
        groups[0].generalShader = 0; groups[1].generalShader = 1; groups[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR; groups[2].closestHitShader = 2;
        
        VkDescriptorSetLayout setLayouts[] = { descriptorSetLayout, globalDescriptorSetLayout };
        VkPushConstantRange pcr{ VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR, 0, sizeof(RayTracingPushConstants) };
        VkPipelineLayoutCreateInfo plInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 2, setLayouts, 1, &pcr };
        vkCreatePipelineLayout(device, &plInfo, nullptr, &m_PipelineLayout);
        
        VkRayTracingPipelineCreateInfoKHR pInfo{ VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR, nullptr, 0, 3, stages, 3, groups, 2, nullptr, nullptr, VK_NULL_HANDLE, m_PipelineLayout };
        vkCreateRayTracingPipelinesKHR(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pInfo, nullptr, &m_Pipeline);
        
        for(auto& stage : stages) vkDestroyShaderModule(device, stage.module, nullptr);
    }

    void RayTracedRenderPath::CreateShaderBindingTable() {
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR };
        VkPhysicalDeviceProperties2 props2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &rtProps };
        vkGetPhysicalDeviceProperties2(m_Context->GetPhysicalDevice(), &props2);
        uint32_t handleSize = rtProps.shaderGroupHandleSize, align = rtProps.shaderGroupBaseAlignment, handleSizeAligned = alignedSize(handleSize, align);
        std::vector<uint8_t> handles(3 * handleSize);
        vkGetRayTracingShaderGroupHandlesKHR(m_Context->GetDevice(), m_Pipeline, 0, 3, 3 * handleSize, handles.data());
        m_SBTBuffer = std::make_unique<Buffer>(m_Context->GetAllocator(), 3 * handleSizeAligned, VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        uint8_t* pData = (uint8_t*)m_SBTBuffer->Map();
        for(int i=0; i<3; ++i) memcpy(pData + i * handleSizeAligned, handles.data() + i * handleSize, handleSize);
        m_SBTBuffer->Unmap();
        VkDeviceAddress addr = m_SBTBuffer->GetDeviceAddress();
        m_RaygenRegion = { addr, handleSizeAligned, handleSizeAligned };
        m_MissRegion = { addr + handleSizeAligned, handleSizeAligned, handleSizeAligned };
        m_HitRegion = { addr + 2 * handleSizeAligned, handleSizeAligned, handleSizeAligned };
    }

    void RayTracedRenderPath::CreateStorageImage() {
        m_StorageImage = std::make_unique<Image>(m_Context->GetAllocator(), m_Context->GetDevice(), m_Context->GetSwapChainExtent().width, m_Context->GetSwapChainExtent().height, m_StorageImageFormat, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        RenderPathUtils::TransitionImageLayout(m_Context, m_StorageImage->GetImage(), m_StorageImageFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1);
    }

    void RayTracedRenderPath::CreateAccumulationImage() {
        m_AccumulationImage = std::make_unique<Image>(m_Context->GetAllocator(), m_Context->GetDevice(), m_Context->GetSwapChainExtent().width, m_Context->GetSwapChainExtent().height, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        RenderPathUtils::TransitionImageLayout(m_Context, m_AccumulationImage->GetImage(), VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1);
    }

    void RayTracedRenderPath::CreateRayTracingDescriptorSetLayout() {
        VkDescriptorSetLayoutBinding b[5];
        b[0] = { 0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, nullptr };
        b[1] = { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_RAYGEN_BIT_KHR, nullptr };
        b[2] = { 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_RAYGEN_BIT_KHR, nullptr };
        b[3] = { 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, nullptr };
        b[4] = { 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, nullptr };
        VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 5, b };
        vkCreateDescriptorSetLayout(m_Context->GetDevice(), &info, nullptr, &m_RTDescriptorSetLayout);
    }

    void RayTracedRenderPath::CreateRayTracingDescriptorSets() {
        VkDescriptorPoolSize ps[3];
        ps[0] = { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 3 }; 
        ps[1] = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 6 }; 
        ps[2] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6 };
        
        VkDescriptorPoolCreateInfo pInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 3, 3, ps };
        vkCreateDescriptorPool(m_Context->GetDevice(), &pInfo, nullptr, &m_RTDescriptorPool);
        
        std::vector<VkDescriptorSetLayout> layouts(3, m_RTDescriptorSetLayout);
        VkDescriptorSetAllocateInfo aInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, m_RTDescriptorPool, 3, layouts.data() };
        m_RTDescriptorSets.resize(3);
        vkAllocateDescriptorSets(m_Context->GetDevice(), &aInfo, m_RTDescriptorSets.data());
        
        VkAccelerationStructureKHR tlas = m_Scene->GetTLAS();
        for (size_t i = 0; i < 3; i++) {
            std::vector<VkWriteDescriptorSet> writes;

            // TLAS (Binding 0)
            VkWriteDescriptorSetAccelerationStructureKHR descriptorAS{};
            descriptorAS.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
            descriptorAS.accelerationStructureCount = 1;
            descriptorAS.pAccelerationStructures = &tlas;

            VkWriteDescriptorSet asWrite{};
            asWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            asWrite.pNext = &descriptorAS;
            asWrite.dstSet = m_RTDescriptorSets[i];
            asWrite.dstBinding = 0;
            asWrite.descriptorCount = 1;
            asWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            
            if (tlas) writes.push_back(asWrite);

            // Storage Image (Binding 1)
            VkDescriptorImageInfo storageInfo{ VK_NULL_HANDLE, m_StorageImage->GetView(), VK_IMAGE_LAYOUT_GENERAL };
            VkWriteDescriptorSet storageWrite{};
            storageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            storageWrite.dstSet = m_RTDescriptorSets[i];
            storageWrite.dstBinding = 1;
            storageWrite.descriptorCount = 1;
            storageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            storageWrite.pImageInfo = &storageInfo;
            writes.push_back(storageWrite);

            // Accum Image (Binding 2)
            VkDescriptorImageInfo accumInfo{ VK_NULL_HANDLE, m_AccumulationImage->GetView(), VK_IMAGE_LAYOUT_GENERAL };
            VkWriteDescriptorSet accumWrite{};
            accumWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            accumWrite.dstSet = m_RTDescriptorSets[i];
            accumWrite.dstBinding = 2;
            accumWrite.descriptorCount = 1;
            accumWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            accumWrite.pImageInfo = &accumInfo;
            writes.push_back(accumWrite);

            // Vertex Buffer (Binding 3)
            VkDescriptorBufferInfo vbInfo{ m_Scene->GetVertexBuffer()->GetBuffer(), 0, VK_WHOLE_SIZE };
            VkWriteDescriptorSet vertexWrite{};
            vertexWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            vertexWrite.dstSet = m_RTDescriptorSets[i];
            vertexWrite.dstBinding = 3;
            vertexWrite.descriptorCount = 1;
            vertexWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            vertexWrite.pBufferInfo = &vbInfo;
            if (m_Scene->GetVertexCount() > 0) writes.push_back(vertexWrite);

            // Index Buffer (Binding 4)
            VkDescriptorBufferInfo ibInfo{ m_Scene->GetIndexBuffer()->GetBuffer(), 0, VK_WHOLE_SIZE };
            VkWriteDescriptorSet indexWrite{};
            indexWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            indexWrite.dstSet = m_RTDescriptorSets[i];
            indexWrite.dstBinding = 4;
            indexWrite.descriptorCount = 1;
            indexWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            indexWrite.pBufferInfo = &ibInfo;
            if (m_Scene->GetIndexCount() > 0) writes.push_back(indexWrite);

            vkUpdateDescriptorSets(m_Context->GetDevice(), (uint32_t)writes.size(), writes.data(), 0, nullptr);
        }
    }

    VkTransformMatrixKHR RayTracedRenderPath::ToVkMatrix(glm::mat4 model) {
        glm::mat4 t = glm::transpose(model); VkTransformMatrixKHR out; memcpy(&out, &t, sizeof(out)); return out;
    }

    uint64_t RayTracedRenderPath::GetAccelerationStructureDeviceAddress(VkAccelerationStructureKHR as) {
        VkAccelerationStructureDeviceAddressInfoKHR info{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR, nullptr, as };
        return vkGetAccelerationStructureDeviceAddressKHR(m_Context->GetDevice(), &info);
    }
}



