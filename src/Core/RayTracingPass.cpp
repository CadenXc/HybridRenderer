#include "pch.h"
#include "RayTracingPass.h"
#include "FileIO.h"

#include "Application.h"

namespace Chimera {

    RayTracingPass::RayTracingPass(std::shared_ptr<VulkanContext> context, ResourceManager* resourceManager)
        : m_Context(context), m_ResourceManager(resourceManager)
    {
    }

    RayTracingPass::~RayTracingPass()
    {
        if (m_RayTracingPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_Context->GetDevice(), m_RayTracingPipeline, nullptr);
        }
        if (m_RayTracingPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_Context->GetDevice(), m_RayTracingPipelineLayout, nullptr);
        }
    }

    void RayTracingPass::Init(VkAccelerationStructureKHR topLevelAS, Image* storageImage,
                              VkDescriptorSetLayout rtDescriptorSetLayout, VkDescriptorSetLayout graphicsDescriptorSetLayout)
    {
        m_TopLevelAS = topLevelAS;
        m_StorageImage = storageImage;
        m_StorageImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        CreateRayTracingPipeline(rtDescriptorSetLayout, graphicsDescriptorSetLayout);
        CreateShaderBindingTable(3); // raygen, miss, closest-hit
    }

    void RayTracingPass::Execute(VkCommandBuffer cmd, const RenderContext& renderContext, 
                                 const std::vector<VkDescriptorSet>& rtDescriptorSets,
                                 VkDescriptorSet graphicsDescriptorSet)
    {
        if (m_RayTracingPipeline == VK_NULL_HANDLE) {
            CH_CORE_ERROR("RayTracingPass::Execute - Pipeline is NULL!");
            return;
        }

        // 1. Transition storage image from current layout to GENERAL (writable)
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = m_StorageImageLayout;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcAccessMask = (m_StorageImageLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) 
            ? VK_ACCESS_TRANSFER_READ_BIT : 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.image = m_StorageImage->GetImage();
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags srcStage = (m_StorageImageLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
            ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;

        vkCmdPipelineBarrier(cmd, srcStage, dstStage, 
                           0, 0, nullptr, 0, nullptr, 1, &barrier);

        m_StorageImageLayout = VK_IMAGE_LAYOUT_GENERAL;

        // 2. Bind ray tracing pipeline
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_RayTracingPipeline);
        
        // 3. Bind both descriptor sets
        if (rtDescriptorSets.size() > renderContext.frameIndex) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_RayTracingPipelineLayout, 0, 1, 
                                   &rtDescriptorSets[renderContext.frameIndex], 0, nullptr);
        }
        
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_RayTracingPipelineLayout, 1, 1,
                               &graphicsDescriptorSet, 0, nullptr);

        // 4. Trace rays
        vkCmdTraceRaysKHR(cmd, &m_RaygenRegion, &m_MissRegion, &m_HitRegion, &m_CallableRegion, 
                         m_WindowWidth, m_WindowHeight, 1);
    }

    void RayTracingPass::OnResize(uint32_t width, uint32_t height)
    {
        m_WindowWidth = width;
        m_WindowHeight = height;
    }

    void RayTracingPass::CreateRayTracingPipeline(VkDescriptorSetLayout rtDescriptorSetLayout, 
                                                   VkDescriptorSetLayout graphicsDescriptorSetLayout)
    {
        // Cleanup existing pipeline resources if any
        if (m_RayTracingPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_Context->GetDevice(), m_RayTracingPipeline, nullptr);
            m_RayTracingPipeline = VK_NULL_HANDLE;
        }
        if (m_RayTracingPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_Context->GetDevice(), m_RayTracingPipelineLayout, nullptr);
            m_RayTracingPipelineLayout = VK_NULL_HANDLE;
        }

        // Load shader modules
        try {
            auto rgenShaderCode = FileIO::ReadFile(std::string(Config::SHADER_DIR) + "raygen.rgen.spv");
            auto missShaderCode = FileIO::ReadFile(std::string(Config::SHADER_DIR) + "miss.rmiss.spv");
            auto chitShaderCode = FileIO::ReadFile(std::string(Config::SHADER_DIR) + "closesthit.rchit.spv");

            VkShaderModule rgenModule = LoadShaderModule(rgenShaderCode);
            VkShaderModule missModule = LoadShaderModule(missShaderCode);
            VkShaderModule chitModule = LoadShaderModule(chitShaderCode);

            // Create shader stages
            std::vector<VkPipelineShaderStageCreateInfo> shaderStages(3);
            shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shaderStages[0].stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
            shaderStages[0].module = rgenModule;
            shaderStages[0].pName = "main";

            shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shaderStages[1].stage = VK_SHADER_STAGE_MISS_BIT_KHR;
            shaderStages[1].module = missModule;
            shaderStages[1].pName = "main";

            shaderStages[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shaderStages[2].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
            shaderStages[2].module = chitModule;
            shaderStages[2].pName = "main";

            // Create shader groups
            std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups(3);
            shaderGroups[0].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            shaderGroups[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            shaderGroups[0].generalShader = 0;
            shaderGroups[0].closestHitShader = VK_SHADER_UNUSED_KHR;
            shaderGroups[0].anyHitShader = VK_SHADER_UNUSED_KHR;
            shaderGroups[0].intersectionShader = VK_SHADER_UNUSED_KHR;

            shaderGroups[1].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            shaderGroups[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            shaderGroups[1].generalShader = 1;
            shaderGroups[1].closestHitShader = VK_SHADER_UNUSED_KHR;
            shaderGroups[1].anyHitShader = VK_SHADER_UNUSED_KHR;
            shaderGroups[1].intersectionShader = VK_SHADER_UNUSED_KHR;

            shaderGroups[2].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            shaderGroups[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
            shaderGroups[2].generalShader = VK_SHADER_UNUSED_KHR;
            shaderGroups[2].closestHitShader = 2;
            shaderGroups[2].anyHitShader = VK_SHADER_UNUSED_KHR;
            shaderGroups[2].intersectionShader = VK_SHADER_UNUSED_KHR;

            // Create pipeline layout
            std::vector<VkDescriptorSetLayout> setLayouts = { rtDescriptorSetLayout, graphicsDescriptorSetLayout };
            VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
            pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
            pipelineLayoutInfo.pSetLayouts = setLayouts.data();

            VkResult layoutResult = vkCreatePipelineLayout(m_Context->GetDevice(), &pipelineLayoutInfo, nullptr, &m_RayTracingPipelineLayout);
            if (layoutResult != VK_SUCCESS) {
                throw std::runtime_error("failed to create RT pipeline layout!");
            }

            // Create ray tracing pipeline
            VkRayTracingPipelineCreateInfoKHR pipelineInfo{};
            pipelineInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
            pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
            pipelineInfo.pStages = shaderStages.data();
            pipelineInfo.groupCount = static_cast<uint32_t>(shaderGroups.size());
            pipelineInfo.pGroups = shaderGroups.data();
            pipelineInfo.maxPipelineRayRecursionDepth = Config::RT_MAX_RECURSION_DEPTH;
            pipelineInfo.layout = m_RayTracingPipelineLayout;

            VkResult pipelineResult = vkCreateRayTracingPipelinesKHR(m_Context->GetDevice(), VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_RayTracingPipeline);
            
            if (pipelineResult != VK_SUCCESS) {
                throw std::runtime_error("failed to create RT pipeline!");
            }

            vkDestroyShaderModule(m_Context->GetDevice(), rgenModule, nullptr);
            vkDestroyShaderModule(m_Context->GetDevice(), missModule, nullptr);
            vkDestroyShaderModule(m_Context->GetDevice(), chitModule, nullptr);
        }
        catch (const std::exception& e) {
            CH_CORE_ERROR("RayTracingPass::CreateRayTracingPipeline - Exception: {}", e.what());
            throw;
        }
    }

    void RayTracingPass::CreateShaderBindingTable(uint32_t groupCount)
    {
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR pipelineProperties{};
        pipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;

        VkPhysicalDeviceProperties2 deviceProperties2{};
        deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        deviceProperties2.pNext = &pipelineProperties;
        vkGetPhysicalDeviceProperties2(m_Context->GetPhysicalDevice(), &deviceProperties2);

        uint32_t handleSize = pipelineProperties.shaderGroupHandleSize;
        uint32_t handleAlignment = pipelineProperties.shaderGroupHandleAlignment;
        uint32_t baseAlignment = pipelineProperties.shaderGroupBaseAlignment;
        uint32_t handleSizeAligned = align_up(handleSize, handleAlignment);

        uint32_t rgenStride = align_up(handleSizeAligned, baseAlignment);
        uint32_t missStride = align_up(handleSizeAligned, baseAlignment);
        uint32_t hitStride  = align_up(handleSizeAligned, baseAlignment);
        VkDeviceSize sbtSize = rgenStride + missStride + hitStride;

        m_ShaderBindingTableBuffer = std::make_unique<Buffer>(
            m_Context->GetAllocator(),
            sbtSize,
            VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );

        std::vector<uint8_t> handles(groupCount * handleSize);
        vkGetRayTracingShaderGroupHandlesKHR(m_Context->GetDevice(), m_RayTracingPipeline, 0, groupCount, groupCount * handleSize, handles.data());

        uint8_t* pData = reinterpret_cast<uint8_t*>(m_ShaderBindingTableBuffer->Map());
        VkDeviceAddress sbtAddress = m_ShaderBindingTableBuffer->GetDeviceAddress();

        memcpy(pData, handles.data(), handleSize);
        m_RaygenRegion.deviceAddress = sbtAddress;
        m_RaygenRegion.stride = rgenStride;
        m_RaygenRegion.size = rgenStride;

        pData += rgenStride;
        memcpy(pData, handles.data() + handleSize, handleSize);
        m_MissRegion.deviceAddress = sbtAddress + rgenStride;
        m_MissRegion.stride = missStride;
        m_MissRegion.size = missStride;

        pData += missStride;
        memcpy(pData, handles.data() + 2 * handleSize, handleSize);
        m_HitRegion.deviceAddress = sbtAddress + rgenStride + missStride;
        m_HitRegion.stride = hitStride;
        m_HitRegion.size = hitStride;

        m_ShaderBindingTableBuffer->Unmap();
    }

    VkShaderModule RayTracingPass::LoadShaderModule(const std::vector<char>& code)
    {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(m_Context->GetDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create shader module!");
        }
        return shaderModule;
    }

    uint32_t RayTracingPass::align_up(uint32_t value, uint32_t alignment)
    {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    uint64_t RayTracingPass::getAccelerationStructureDeviceAddress(VkAccelerationStructureKHR as)
    {
        VkAccelerationStructureDeviceAddressInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        info.accelerationStructure = as;
        return vkGetAccelerationStructureDeviceAddressKHR(m_Context->GetDevice(), &info);
    }

}

