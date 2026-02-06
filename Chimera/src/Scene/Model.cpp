#include "pch.h"
#include "Model.h"
#include "Core/Log.h"

namespace Chimera {

    Model::Model(std::shared_ptr<VulkanContext> context, const ImportedScene& importedScene)
        : m_Context(context), m_VertexCount((uint32_t)importedScene.Vertices.size()), m_IndexCount((uint32_t)importedScene.Indices.size())
    {
        m_Meshes = importedScene.Meshes;

        // 1. Create Vertex Buffer
        {
            VkDeviceSize bufferSize = sizeof(Vertex) * importedScene.Vertices.size();
            Buffer stagingBuffer(m_Context->GetAllocator(), bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
            stagingBuffer.UploadData(importedScene.Vertices.data(), bufferSize);

            m_VertexBuffer = std::make_unique<Buffer>(
                m_Context->GetAllocator(), 
                bufferSize, 
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, 
                VMA_MEMORY_USAGE_GPU_ONLY
            );

            VkCommandBuffer cmd = m_Context->BeginSingleTimeCommands();
            VkBufferCopy copyRegion{ 0, 0, bufferSize };
            vkCmdCopyBuffer(cmd, stagingBuffer.GetBuffer(), m_VertexBuffer->GetBuffer(), 1, &copyRegion);
            m_Context->EndSingleTimeCommands(cmd);
        }

        // 2. Create Index Buffer
        {
            VkDeviceSize bufferSize = sizeof(uint32_t) * importedScene.Indices.size();
            Buffer stagingBuffer(m_Context->GetAllocator(), bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
            stagingBuffer.UploadData(importedScene.Indices.data(), bufferSize);

            m_IndexBuffer = std::make_unique<Buffer>(
                m_Context->GetAllocator(), 
                bufferSize, 
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, 
                VMA_MEMORY_USAGE_GPU_ONLY
            );

            VkCommandBuffer cmd = m_Context->BeginSingleTimeCommands();
            VkBufferCopy copyRegion{ 0, 0, bufferSize };
            vkCmdCopyBuffer(cmd, stagingBuffer.GetBuffer(), m_IndexBuffer->GetBuffer(), 1, &copyRegion);
            m_Context->EndSingleTimeCommands(cmd);
        }

        // 3. Build BLAS
        BuildBLAS();
    }

    Model::~Model()
    {
        auto device = m_Context->GetDevice();
        for (auto as : m_BLASHandles)
        {
            if (as != VK_NULL_HANDLE)
                vkDestroyAccelerationStructureKHR(device, as, nullptr);
        }
    }

    void Model::BuildBLAS()
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
            geometry.geometry.triangles.maxVertex = m_VertexCount;
            
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
}
