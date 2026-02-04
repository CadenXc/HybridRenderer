#include "pch.h"
#include "Scene/Scene.h"
#include "Assets/AssetImporter.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Resources/Buffer.h"
#include "Core/Log.h"

namespace Chimera
{
    Scene::Scene(std::shared_ptr<VulkanContext> context, ResourceManager* resourceManager)
        : m_Context(context), m_ResourceManager(resourceManager)
    {
        m_Camera.view = glm::lookAt(glm::vec3(0.0f, 2.0f, 5.0f), glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        m_Camera.proj = glm::perspective(glm::radians(45.0f), context->GetSwapChainExtent().width / (float)context->GetSwapChainExtent().height, 0.1f, 1000.0f);
        m_Camera.proj[1][1] *= -1; 
        
        m_Camera.viewInverse = glm::inverse(m_Camera.view);
        m_Camera.projInverse = glm::inverse(m_Camera.proj);

        m_Light.direction = glm::vec4(1.0f, -3.0f, 1.0f, 0.0f);
        m_Light.color = glm::vec4(1.0f, 1.0f, 1.0f, 5.0f);
    }

    Scene::~Scene()
    {
        auto device = m_Context->GetDevice();
        if (m_TopLevelAS != VK_NULL_HANDLE) 
            vkDestroyAccelerationStructureKHR(device, m_TopLevelAS, nullptr);
        
        for (auto as : m_BLASHandles) 
            if (as != VK_NULL_HANDLE) vkDestroyAccelerationStructureKHR(device, as, nullptr);
    }

    void Scene::LoadModel(const std::string& path)
    {
        CH_CORE_INFO("Loading model via AssetImporter: {0}", path);
        
        auto importedScene = AssetImporter::ImportScene(path, m_ResourceManager);
        if (!importedScene) return;

        // 1. Cleanup old Vulkan resources
        auto device = m_Context->GetDevice();
        if (m_TopLevelAS != VK_NULL_HANDLE) {
            vkDestroyAccelerationStructureKHR(device, m_TopLevelAS, nullptr);
            m_TopLevelAS = VK_NULL_HANDLE;
        }
        for (auto as : m_BLASHandles) 
            vkDestroyAccelerationStructureKHR(device, as, nullptr);
        
        m_BLASHandles.clear();
        m_BLASBuffers.clear();
        m_TLASBuffer.reset();

        // 2. Transfer data
        m_Meshes = std::move(importedScene->Meshes);
        m_Materials = std::move(importedScene->Materials);
        m_VertexCount = importedScene->Vertices.size();
        m_IndexCount = importedScene->Indices.size();

        // 3. Create GPU resources
        CreateVertexBuffer(importedScene->Vertices);
        CreateIndexBuffer(importedScene->Indices);
        
        // 4. Rebuild AS
        BuildBLAS();
        BuildTLAS();
    }

    void Scene::CreateVertexBuffer(const std::vector<Vertex>& vertices)
    {
        VkDeviceSize bufferSize = sizeof(Vertex) * vertices.size();
        
        Buffer stagingBuffer(m_Context->GetAllocator(), bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
        stagingBuffer.UploadData(vertices.data(), bufferSize);

        m_VertexBuffer = std::make_unique<Buffer>(
            m_Context->GetAllocator(), 
            bufferSize, 
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, 
            VMA_MEMORY_USAGE_GPU_ONLY
        );

        CopyBuffer(stagingBuffer.GetBuffer(), m_VertexBuffer->GetBuffer(), bufferSize);
    }

    void Scene::CreateIndexBuffer(const std::vector<uint32_t>& indices)
    {
        VkDeviceSize bufferSize = sizeof(uint32_t) * indices.size();

        Buffer stagingBuffer(m_Context->GetAllocator(), bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
        stagingBuffer.UploadData(indices.data(), bufferSize);

        m_IndexBuffer = std::make_unique<Buffer>(
            m_Context->GetAllocator(), 
            bufferSize, 
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, 
            VMA_MEMORY_USAGE_GPU_ONLY
        );

        CopyBuffer(stagingBuffer.GetBuffer(), m_IndexBuffer->GetBuffer(), bufferSize);
    }

    void Scene::BuildBLAS()
    {
        VkDeviceAddress vertexAddress = m_VertexBuffer->GetDeviceAddress();
        VkDeviceAddress indexAddress = m_IndexBuffer->GetDeviceAddress();

        for (const auto& mesh : m_Meshes)
        {
            VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
            geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

            geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
            geometry.geometry.triangles.vertexData.deviceAddress = vertexAddress; 
            geometry.geometry.triangles.vertexStride = sizeof(Vertex);
            geometry.geometry.triangles.maxVertex = (uint32_t)m_VertexCount;
            
            geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
            geometry.geometry.triangles.indexData.deviceAddress = indexAddress;

            VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
            buildRangeInfo.primitiveCount = mesh.indexCount / 3;
            buildRangeInfo.primitiveOffset = mesh.indexOffset * sizeof(uint32_t); 

            VkAccelerationStructureBuildGeometryInfoKHR buildInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
            buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
            buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
            buildInfo.geometryCount = 1;
            buildInfo.pGeometries = &geometry;

            VkAccelerationStructureBuildSizesInfoKHR sizeInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
            vkGetAccelerationStructureBuildSizesKHR(m_Context->GetDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &buildRangeInfo.primitiveCount, &sizeInfo);

            auto blasBuffer = std::make_unique<Buffer>(m_Context->GetAllocator(), sizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
            
            VkAccelerationStructureCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
            createInfo.buffer = blasBuffer->GetBuffer();
            createInfo.size = sizeInfo.accelerationStructureSize;
            createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            
            VkAccelerationStructureKHR handle;
            vkCreateAccelerationStructureKHR(m_Context->GetDevice(), &createInfo, nullptr, &handle);

            Buffer scratchBuffer(m_Context->GetAllocator(), sizeInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
            buildInfo.dstAccelerationStructure = handle;
            buildInfo.scratchData.deviceAddress = scratchBuffer.GetDeviceAddress();

            VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &buildRangeInfo; 
            
            VkCommandBuffer cmd = m_Context->BeginSingleTimeCommands();
            vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRangeInfo);
            m_Context->EndSingleTimeCommands(cmd);

            m_BLASBuffers.push_back(std::move(blasBuffer));
            m_BLASHandles.push_back(handle);
        }
    }

    void Scene::BuildTLAS()
    {
        if (m_BLASHandles.empty()) return;

        std::vector<VkAccelerationStructureInstanceKHR> instances;
        for (size_t i = 0; i < m_BLASHandles.size(); ++i)
        {
            VkAccelerationStructureDeviceAddressInfoKHR addressInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
            addressInfo.accelerationStructure = m_BLASHandles[i];
            VkDeviceAddress blasAddress = vkGetAccelerationStructureDeviceAddressKHR(m_Context->GetDevice(), &addressInfo);

            VkAccelerationStructureInstanceKHR instance{};
            instance.transform = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
            instance.instanceCustomIndex = (uint32_t)i;
            instance.mask = 0xFF;
            instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
            instance.accelerationStructureReference = blasAddress;
            instances.push_back(instance);
        }

        VkDeviceSize instanceBufferSize = sizeof(VkAccelerationStructureInstanceKHR) * instances.size();
        Buffer instanceBuffer(m_Context->GetAllocator(), instanceBufferSize, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        
        Buffer stagingBuffer(m_Context->GetAllocator(), instanceBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
        stagingBuffer.UploadData(instances.data(), instanceBufferSize);
        CopyBuffer(stagingBuffer.GetBuffer(), instanceBuffer.GetBuffer(), instanceBufferSize);

        VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
        geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        geometry.geometry.instances.data.deviceAddress = instanceBuffer.GetDeviceAddress();

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geometry;

        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        uint32_t primitiveCount = (uint32_t)instances.size();
        vkGetAccelerationStructureBuildSizesKHR(m_Context->GetDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primitiveCount, &sizeInfo);

        m_TLASBuffer = std::make_unique<Buffer>(m_Context->GetAllocator(), sizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

        VkAccelerationStructureCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
        createInfo.buffer = m_TLASBuffer->GetBuffer();
        createInfo.size = sizeInfo.accelerationStructureSize;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        vkCreateAccelerationStructureKHR(m_Context->GetDevice(), &createInfo, nullptr, &m_TopLevelAS);

        Buffer scratchBuffer(m_Context->GetAllocator(), sizeInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        buildInfo.dstAccelerationStructure = m_TopLevelAS;
        buildInfo.scratchData.deviceAddress = scratchBuffer.GetDeviceAddress();

        VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{ primitiveCount, 0, 0, 0 };
        VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &buildRangeInfo;

        VkCommandBuffer cmd = m_Context->BeginSingleTimeCommands();
        vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRangeInfo);
        m_Context->EndSingleTimeCommands(cmd);
    }

    void Scene::CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
    {
        VkCommandBuffer commandBuffer = m_Context->BeginSingleTimeCommands();
        VkBufferCopy copyRegion{ 0, 0, size };
        vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
        m_Context->EndSingleTimeCommands(commandBuffer);
    }
}
