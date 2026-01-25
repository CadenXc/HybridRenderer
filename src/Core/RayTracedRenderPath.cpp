#include "pch.h"
#include "RayTracedRenderPath.h"
#include "Application.h" // For config if needed, or define constants here

#include <imgui.h>

namespace Chimera {

    // Ideally these should be in a Config class or passed in, but for now we duplicate or reference Config
    // We can assume Config::WINDOW_WIDTH etc are available if we include Application.h or move Config to a separate header.
    // For now, I'll hardcode or use the Application::Config namespace if accessible.
    // Application.h includes them.
    
    // Barrier Helpers (Moved from Application.cpp or duplicated)
    static VkImageMemoryBarrier CreateImageBarrier(
        VkImage image,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        VkAccessFlags srcAccess,
        VkAccessFlags dstAccess,
        VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT
    ) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = dstAccess;
        barrier.subresourceRange.aspectMask = aspectMask;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        return barrier;
    }

    static void CmdPipelineBarrier(
        VkCommandBuffer commandBuffer,
        VkPipelineStageFlags srcStage,
        VkPipelineStageFlags dstStage,
        const VkImageMemoryBarrier& barrier
    ) {
        vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    RayTracedRenderPath::RayTracedRenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager, VkDescriptorSetLayout globalDescriptorSetLayout)
        : RenderPath(context, scene, resourceManager), m_GlobalDescriptorSetLayout(globalDescriptorSetLayout)
    {
    }

    RayTracedRenderPath::~RayTracedRenderPath()
    {
        // Cleanup
        auto device = m_Context->GetDevice();
        
        m_RayTracingPass.reset();

        if (m_RTDescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, m_RTDescriptorPool, nullptr);
        }
        if (m_RTDescriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, m_RTDescriptorSetLayout, nullptr);
        }
        if (m_TopLevelAS != VK_NULL_HANDLE) {
            vkDestroyAccelerationStructureKHR(device, m_TopLevelAS, nullptr);
        }

        m_StorageImage.reset();
        m_AccumulationImage.reset();
        m_TLASBuffer.reset();
    }

    void RayTracedRenderPath::Init()
    {
        CreateTopLevelAS();
        CreateStorageImage();
        CreateAccumulationImage();
        CreateRayTracingDescriptorSetLayout();
        CreateRayTracingDescriptorSets();

        m_RayTracingPass = std::make_unique<RayTracingPass>(m_Context, m_ResourceManager);
        m_RayTracingPass->Init(m_TopLevelAS, m_StorageImage.get(), m_RTDescriptorSetLayout, m_GlobalDescriptorSetLayout);
    }

    void RayTracedRenderPath::OnResize(uint32_t width, uint32_t height)
    {
        m_RayTracingPass->OnResize(width, height);
        
        // Recreate storage images
        m_StorageImage.reset();
        m_AccumulationImage.reset();
        CreateStorageImage();
        CreateAccumulationImage();

        // Update descriptors
        // This is a bit heavy, usually we update the descriptor sets pointing to the new images
        // For now, let's just re-write the descriptors.
        
        auto device = m_Context->GetDevice();
        
        // Update descriptor sets for all frames
        for (size_t i = 0; i < m_RTDescriptorSets.size(); i++) 
        {
            VkDescriptorImageInfo storageImageInfo{};
            storageImageInfo.imageView = m_StorageImage->GetView();
            storageImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkWriteDescriptorSet storageImageWrite{};
            storageImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            storageImageWrite.dstSet = m_RTDescriptorSets[i];
            storageImageWrite.dstBinding = 1;
            storageImageWrite.descriptorCount = 1;
            storageImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            storageImageWrite.pImageInfo = &storageImageInfo;

            VkDescriptorImageInfo accumImageInfo{};
            accumImageInfo.imageView = m_AccumulationImage->GetView();
            accumImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkWriteDescriptorSet accumImageWrite{};
            accumImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            accumImageWrite.dstSet = m_RTDescriptorSets[i];
            accumImageWrite.dstBinding = 2;
            accumImageWrite.descriptorCount = 1;
            accumImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            accumImageWrite.pImageInfo = &accumImageInfo;

            std::vector<VkWriteDescriptorSet> descriptorWrites = { storageImageWrite, accumImageWrite };
            vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
        }
        
        // Update Pass image reference
        m_RayTracingPass->Init(m_TopLevelAS, m_StorageImage.get(), m_RTDescriptorSetLayout, m_GlobalDescriptorSetLayout);
    }

    void RayTracedRenderPath::OnSceneUpdated()
    {
        // Rebuild TLAS
        if (m_TopLevelAS != VK_NULL_HANDLE) {
            vkDestroyAccelerationStructureKHR(m_Context->GetDevice(), m_TopLevelAS, nullptr);
            m_TopLevelAS = VK_NULL_HANDLE;
        }
        m_TLASBuffer.reset();
        CreateTopLevelAS();

        // Update Descriptors (Vertex/Index buffers changed)
        // We can just recreate them or update them. Recreating is safer/easier given existing code structure.
        if (m_RTDescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_Context->GetDevice(), m_RTDescriptorPool, nullptr);
            m_RTDescriptorPool = VK_NULL_HANDLE;
        }
        CreateRayTracingDescriptorSets();

        // Update Pass
        m_RayTracingPass->Init(m_TopLevelAS, m_StorageImage.get(), m_RTDescriptorSetLayout, m_GlobalDescriptorSetLayout);
    }

    void RayTracedRenderPath::OnImGui()
    {
        ImGui::Begin("Ray Traced Render Settings");
        ImGui::Text("Ray Tracing Enabled");
        // Add more settings here (e.g. max recursion depth, samples per pixel)
        ImGui::End();
    }

    void RayTracedRenderPath::Render(VkCommandBuffer commandBuffer, uint32_t currentFrame, uint32_t imageIndex,  
                                     VkDescriptorSet globalDescriptorSet, const std::vector<VkImage>& swapChainImages,
                                     std::function<void(VkCommandBuffer)> uiDrawCallback)
    {
        RenderContext renderContext;
        renderContext.commandBuffer = commandBuffer;
        renderContext.frameIndex = currentFrame;
        renderContext.imageIndex = imageIndex;

        // Execute Ray Tracing Pass
        m_RayTracingPass->Execute(commandBuffer, renderContext, m_RTDescriptorSets, globalDescriptorSet);

        // Copy to Swapchain
        auto swapChainBarrier = CreateImageBarrier(
            swapChainImages[imageIndex],
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            0,
            VK_ACCESS_TRANSFER_WRITE_BIT
        );
        
        auto storageTransferBarrier = CreateImageBarrier(
            m_StorageImage->GetImage(),
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT
        );

        CmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_TRANSFER_BIT, swapChainBarrier);
        CmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_TRANSFER_BIT, storageTransferBarrier);

        // Copy
        VkImageCopy copyRegion{};
        copyRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copyRegion.srcOffset = { 0, 0, 0 };
        copyRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copyRegion.dstOffset = { 0, 0, 0 };
        VkExtent3D extent = { m_Context->GetSwapChainExtent().width, m_Context->GetSwapChainExtent().height, 1 };
        copyRegion.extent = extent;
        
        vkCmdCopyImage(commandBuffer, m_StorageImage->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapChainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        // Draw UI
        // Note: For RayTracing, we need to begin a rendering pass for ImGui because we are not inside one.
        // Or we can rely on ImGui_ImplVulkan_RenderDrawData which now works with Dynamic Rendering but expects to be inside BeginRendering/EndRendering?
        // ImGui_ImplVulkan_RenderDrawData DOES NOT call BeginRendering itself. We must provide it.
        if (uiDrawCallback) {
            // Prepare Swapchain Image for Attachment
            // It is currently VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
            // We need VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
            
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.image = swapChainImages[imageIndex];
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

            // ImGuiLayer starts its own RenderPass, so we don't need dynamic rendering here.
            uiDrawCallback(commandBuffer);

            // Transition from COLOR_ATTACHMENT to TRANSFER_DST so the final barrier to PRESENT works
            // Or just update the final barrier to accept COLOR_ATTACHMENT
        }

        // Reset storage image layout tracking in pass
        m_RayTracingPass->SetStorageImageLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        // Prepare swapchain for present
        // If UI was drawn, layout is COLOR_ATTACHMENT_OPTIMAL. If not, it is TRANSFER_DST_OPTIMAL.
        VkImageLayout currentLayout = uiDrawCallback ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        VkAccessFlags srcAccess = uiDrawCallback ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : VK_ACCESS_TRANSFER_WRITE_BIT;
        VkPipelineStageFlags srcStage = uiDrawCallback ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT;

        auto presentBarrier = CreateImageBarrier(
            swapChainImages[imageIndex],
            currentLayout,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            srcAccess,
            0
        );

        CmdPipelineBarrier(commandBuffer, srcStage, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, presentBarrier);
    }

    // ================== Implementation of Setup Methods ==================

    void RayTracedRenderPath::CreateTopLevelAS()
    {
        // ... (Logic copied from Application::createTopLevelAS)
        // Need to access m_Scene->GetBLAS()
        
        VkAccelerationStructureInstanceKHR instance{};
        instance.transform = ToVkMatrix(glm::mat4(1.0f));
        instance.instanceCustomIndex = 0;
        instance.mask = 0xFF;
        instance.instanceShaderBindingTableRecordOffset = 0;
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instance.accelerationStructureReference = GetAccelerationStructureDeviceAddress(m_Scene->GetBLAS());

        Buffer instanceBuffer(
            m_Context->GetAllocator(),
            sizeof(instance),
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );
        instanceBuffer.UploadData(&instance, sizeof(instance));

        VkAccelerationStructureGeometryKHR geometry{};
        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        geometry.geometry.instances.arrayOfPointers = VK_FALSE;
        geometry.geometry.instances.data.deviceAddress = instanceBuffer.GetDeviceAddress();

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geometry;

        uint32_t primitiveCount = 1;
        VkAccelerationStructureBuildSizesInfoKHR buildSizesInfo{};
        buildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        vkGetAccelerationStructureBuildSizesKHR(m_Context->GetDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primitiveCount, &buildSizesInfo);

        m_TLASBuffer = std::make_unique<Buffer>(
            m_Context->GetAllocator(),
            buildSizesInfo.accelerationStructureSize,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY
        );

        VkAccelerationStructureCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        createInfo.buffer = m_TLASBuffer->GetBuffer();
        createInfo.size = buildSizesInfo.accelerationStructureSize;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        vkCreateAccelerationStructureKHR(m_Context->GetDevice(), &createInfo, nullptr, &m_TopLevelAS);

        Buffer scratchBuffer(
            m_Context->GetAllocator(),
            buildSizesInfo.buildScratchSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY
        );
        
        buildInfo.scratchData.deviceAddress = scratchBuffer.GetDeviceAddress();
        buildInfo.dstAccelerationStructure = m_TopLevelAS;

        VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
        rangeInfo.primitiveCount = 1;
        rangeInfo.primitiveOffset = 0;
        rangeInfo.firstVertex = 0;
        rangeInfo.transformOffset = 0;
        const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

        VkCommandBuffer commandBuffer = BeginSingleTimeCommands();
        vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildInfo, &pRangeInfo);
        EndSingleTimeCommands(commandBuffer);
    }

    void RayTracedRenderPath::CreateStorageImage()
    {
        m_StorageImage = std::make_unique<Image>(
            m_Context->GetAllocator(),
            m_Context->GetDevice(),
            m_Context->GetSwapChainExtent().width,
            m_Context->GetSwapChainExtent().height,
            m_StorageImageFormat,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT
        );

        TransitionImageLayoutImmediate(m_StorageImage->GetImage(), m_StorageImageFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1);
    }

    void RayTracedRenderPath::CreateAccumulationImage()
    {
        m_AccumulationImage = std::make_unique<Image>(
            m_Context->GetAllocator(),
            m_Context->GetDevice(),
            m_Context->GetSwapChainExtent().width,
            m_Context->GetSwapChainExtent().height,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT
        );

        TransitionImageLayoutImmediate(m_AccumulationImage->GetImage(), VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1);
    }

    void RayTracedRenderPath::CreateRayTracingDescriptorSetLayout()
    {
        VkDescriptorSetLayoutBinding asLayoutBinding{};
        asLayoutBinding.binding = 0;
        asLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        asLayoutBinding.descriptorCount = 1;
        asLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        VkDescriptorSetLayoutBinding storageImageBinding{};
        storageImageBinding.binding = 1;
        storageImageBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        storageImageBinding.descriptorCount = 1;
        storageImageBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

        VkDescriptorSetLayoutBinding accumImageBinding{};
        accumImageBinding.binding = 2;
        accumImageBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        accumImageBinding.descriptorCount = 1;
        accumImageBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

        VkDescriptorSetLayoutBinding vertexBufferBinding{};
        vertexBufferBinding.binding = 3;
        vertexBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        vertexBufferBinding.descriptorCount = 1;
        vertexBufferBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        VkDescriptorSetLayoutBinding indexBufferBinding{};
        indexBufferBinding.binding = 4;
        indexBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        indexBufferBinding.descriptorCount = 1;
        indexBufferBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        std::array<VkDescriptorSetLayoutBinding, 5> bindings = { asLayoutBinding, storageImageBinding, accumImageBinding, vertexBufferBinding, indexBufferBinding };

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(m_Context->GetDevice(), &layoutInfo, nullptr, &m_RTDescriptorSetLayout) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create RT descriptor set layout!");
        }
    }

    void RayTracedRenderPath::CreateRayTracingDescriptorSets()
    {
        // Hardcoded max frames in flight to 3 or use Renderer constant
        const uint32_t MAX_FRAMES = 3; // Should ideally come from Context/Renderer

        std::array<VkDescriptorPoolSize, 3> poolSizes{};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        poolSizes[0].descriptorCount = MAX_FRAMES;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        poolSizes[1].descriptorCount = MAX_FRAMES * 2;
        poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSizes[2].descriptorCount = MAX_FRAMES * 2;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = MAX_FRAMES;

        if (vkCreateDescriptorPool(m_Context->GetDevice(), &poolInfo, nullptr, &m_RTDescriptorPool) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create RT descriptor pool!");
        }

        std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES, m_RTDescriptorSetLayout);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_RTDescriptorPool;
        allocInfo.descriptorSetCount = MAX_FRAMES;
        allocInfo.pSetLayouts = layouts.data();

        m_RTDescriptorSets.resize(MAX_FRAMES);
        if (vkAllocateDescriptorSets(m_Context->GetDevice(), &allocInfo, m_RTDescriptorSets.data()) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to allocate RT descriptor sets!");
        }

        for (size_t i = 0; i < MAX_FRAMES; i++) 
        {
            VkWriteDescriptorSetAccelerationStructureKHR descriptorAS{};
            descriptorAS.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
            descriptorAS.accelerationStructureCount = 1;
            descriptorAS.pAccelerationStructures = &m_TopLevelAS;

            VkWriteDescriptorSet asWrite{};
            asWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            asWrite.pNext = &descriptorAS;
            asWrite.dstSet = m_RTDescriptorSets[i];
            asWrite.dstBinding = 0;
            asWrite.descriptorCount = 1;
            asWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

            VkDescriptorImageInfo storageImageInfo{};
            storageImageInfo.imageView = m_StorageImage->GetView();
            storageImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkWriteDescriptorSet storageImageWrite{};
            storageImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            storageImageWrite.dstSet = m_RTDescriptorSets[i];
            storageImageWrite.dstBinding = 1;
            storageImageWrite.descriptorCount = 1;
            storageImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            storageImageWrite.pImageInfo = &storageImageInfo;

            VkDescriptorImageInfo accumImageInfo{};
            accumImageInfo.imageView = m_AccumulationImage->GetView();
            accumImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkWriteDescriptorSet accumImageWrite{};
            accumImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            accumImageWrite.dstSet = m_RTDescriptorSets[i];
            accumImageWrite.dstBinding = 2;
            accumImageWrite.descriptorCount = 1;
            accumImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            accumImageWrite.pImageInfo = &accumImageInfo;

            VkDescriptorBufferInfo vertexBufferInfo{};
            vertexBufferInfo.buffer = m_Scene->GetVertexBuffer()->GetBuffer();
            vertexBufferInfo.offset = 0;
            vertexBufferInfo.range = VK_WHOLE_SIZE;

            VkWriteDescriptorSet vertexWrite{};
            vertexWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            vertexWrite.dstSet = m_RTDescriptorSets[i];
            vertexWrite.dstBinding = 3;
            vertexWrite.descriptorCount = 1;
            vertexWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            vertexWrite.pBufferInfo = &vertexBufferInfo;

            VkDescriptorBufferInfo indexBufferInfo{};
            indexBufferInfo.buffer = m_Scene->GetIndexBuffer()->GetBuffer();
            indexBufferInfo.offset = 0;
            indexBufferInfo.range = VK_WHOLE_SIZE;

            VkWriteDescriptorSet indexWrite{};
            indexWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            indexWrite.dstSet = m_RTDescriptorSets[i];
            indexWrite.dstBinding = 4;
            indexWrite.descriptorCount = 1;
            indexWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            indexWrite.pBufferInfo = &indexBufferInfo;

            std::vector<VkWriteDescriptorSet> descriptorWrites = { asWrite, storageImageWrite, accumImageWrite, vertexWrite, indexWrite };
            vkUpdateDescriptorSets(m_Context->GetDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
        }
    }

    VkTransformMatrixKHR RayTracedRenderPath::ToVkMatrix(glm::mat4 model)
    {
        glm::mat4 transposed = glm::transpose(model);
        VkTransformMatrixKHR outMatrix;
        memcpy(&outMatrix, &transposed, sizeof(VkTransformMatrixKHR));
        return outMatrix;
    }

    uint64_t RayTracedRenderPath::GetAccelerationStructureDeviceAddress(VkAccelerationStructureKHR as)
    {
        VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        addressInfo.accelerationStructure = as;
        return vkGetAccelerationStructureDeviceAddressKHR(m_Context->GetDevice(), &addressInfo);
    }

    VkCommandBuffer RayTracedRenderPath::BeginSingleTimeCommands()
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

    void RayTracedRenderPath::EndSingleTimeCommands(VkCommandBuffer commandBuffer)
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

    void RayTracedRenderPath::TransitionImageLayoutImmediate(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels)
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

        VkPipelineStageFlags sourceStage, destinationStage;
        // Simplified logic for RT setup
        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_GENERAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
        }
        else
        {
             // Fallback for other transitions if needed, or copy full logic from Application
             // For now we only use this for creation of Storage/Accum images
            if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            } else {
                 throw std::invalid_argument("unsupported layout transition in RenderPath!");
            }
        }
        vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        EndSingleTimeCommands(commandBuffer);
    }

}
