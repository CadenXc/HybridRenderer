#include "pch.h"
#include "Model.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Backend/RenderContext.h"
#include "Renderer/Resources/Buffer.h"
#include "Renderer/Backend/ShaderCommon.h"
#include "Utils/VulkanBarrier.h"

namespace Chimera
{
Model::Model(std::shared_ptr<VulkanContext> context,
             const ImportedScene& importedScene)
    : m_Context(context), m_Status(LoadingStatus::Loading)
{
    UploadToGPU(importedScene);
}

Model::Model(std::shared_ptr<VulkanContext> context)
    : m_Context(context), m_Status(LoadingStatus::Uninitialized)
{
}

void Model::UploadToGPU(const ImportedScene& importedScene)
{
    m_Status = LoadingStatus::Uploading;
    m_VertexCount = (uint32_t)importedScene.Vertices.size();
    m_IndexCount = (uint32_t)importedScene.Indices.size();
    uint32_t triangleCount = (uint32_t)importedScene.Triangles.size();

    if (m_VertexCount == 0 || m_IndexCount == 0)
    {
        CH_CORE_ERROR("Model: Cannot upload empty scene data!");
        m_Status = LoadingStatus::Failed;
        return;
    }

    VkDeviceSize vertexBufferSize = sizeof(GpuVertex) * m_VertexCount;
    VkDeviceSize indexBufferSize = sizeof(uint32_t) * m_IndexCount;
    VkDeviceSize triangleBufferSize = sizeof(GpuTriangle) * triangleCount;

    // 1. Staging
    Buffer stagingVBO(vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VMA_MEMORY_USAGE_CPU_ONLY);
    stagingVBO.Update(importedScene.Vertices.data(), vertexBufferSize);

    Buffer stagingIBO(indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VMA_MEMORY_USAGE_CPU_ONLY);
    stagingIBO.Update(importedScene.Indices.data(), indexBufferSize);

    std::unique_ptr<Buffer> stagingTBO;
    if (triangleCount > 0)
    {
        stagingTBO = std::make_unique<Buffer>(triangleBufferSize,
                                              VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                              VMA_MEMORY_USAGE_CPU_ONLY);
        stagingTBO->Update(importedScene.Triangles.data(), triangleBufferSize);
    }

    // 2. GPU Buffers
    m_VertexBuffer = std::make_unique<Buffer>(
        vertexBufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        VMA_MEMORY_USAGE_GPU_ONLY);

    m_IndexBuffer = std::make_unique<Buffer>(
        indexBufferSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        VMA_MEMORY_USAGE_GPU_ONLY);

    if (triangleCount > 0)
    {
        m_TriangleBuffer = std::make_unique<Buffer>(
            triangleBufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY);
    }

    // 3. Transfer
    {
        ScopedCommandBuffer cmd;
        VkBufferCopy vCopy{0, 0, vertexBufferSize};
        VkBufferCopy iCopy{0, 0, indexBufferSize};
        vkCmdCopyBuffer(cmd, (VkBuffer)stagingVBO.GetBuffer(),
                        (VkBuffer)m_VertexBuffer->GetBuffer(), 1, &vCopy);
        vkCmdCopyBuffer(cmd, (VkBuffer)stagingIBO.GetBuffer(),
                        (VkBuffer)m_IndexBuffer->GetBuffer(), 1, &iCopy);
        if (triangleCount > 0)
        {
            VkBufferCopy tCopy{0, 0, triangleBufferSize};
            vkCmdCopyBuffer(cmd, (VkBuffer)stagingTBO->GetBuffer(),
                            (VkBuffer)m_TriangleBuffer->GetBuffer(), 1, &tCopy);
        }
    }

    m_Meshes = importedScene.Meshes;
    m_TriangleData = importedScene.Triangles;
    m_BLASBuffers.clear();
    m_BLASHandles.clear();

    if (m_Context->IsRayTracingSupported()) BuildBLAS();

    m_Status = LoadingStatus::Ready;
}

void Model::BuildBLAS()
{
    VkDevice device = m_Context->GetDevice();
    for (uint32_t i = 0; i < m_Meshes.size(); ++i)
    {
        const auto& mesh = m_Meshes[i];
        uint32_t maxPrimitiveCount = mesh.indexCount / 3;
        if (maxPrimitiveCount == 0) continue;

        VkAccelerationStructureGeometryKHR geo{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        geo.flags = 0;
        geo.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geo.geometry.triangles.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        geo.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        geo.geometry.triangles.vertexData.deviceAddress =
            m_VertexBuffer->GetDeviceAddress() +
            (mesh.vertexOffset * sizeof(GpuVertex));
        geo.geometry.triangles.vertexStride = sizeof(GpuVertex);
        geo.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
        geo.geometry.triangles.indexData.deviceAddress =
            m_IndexBuffer->GetDeviceAddress() +
            (mesh.indexOffset * sizeof(uint32_t));
        geo.geometry.triangles.maxVertex = m_VertexCount;

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfo.flags =
            VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geo;

        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        vkGetAccelerationStructureBuildSizesKHR(
            device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo,
            &maxPrimitiveCount, &sizeInfo);

        auto blasBuffer = std::make_unique<Buffer>(
            sizeInfo.accelerationStructureSize,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY);
        VkAccelerationStructureCreateInfoKHR createInfo{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
        createInfo.buffer = (VkBuffer)blasBuffer->GetBuffer();
        createInfo.size = sizeInfo.accelerationStructureSize;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

        VkAccelerationStructureKHR handle;
        vkCreateAccelerationStructureKHR(device, &createInfo, nullptr, &handle);

        Buffer scratch(sizeInfo.buildScratchSize,
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                           VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                       VMA_MEMORY_USAGE_GPU_ONLY);
        buildInfo.dstAccelerationStructure = handle;
        buildInfo.scratchData.deviceAddress = scratch.GetDeviceAddress();

        VkAccelerationStructureBuildRangeInfoKHR rangeInfo{maxPrimitiveCount, 0,
                                                           0, 0};
        const VkAccelerationStructureBuildRangeInfoKHR* pRange = &rangeInfo;
        {
            ScopedCommandBuffer cmd;
            vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRange);
        }
        m_BLASBuffers.push_back(std::move(blasBuffer));
        m_BLASHandles.push_back(handle);
    }
}

Model::~Model()
{
    if (m_Context)
    {
        VkDevice device = m_Context->GetDevice();
        if (device != VK_NULL_HANDLE && vkDestroyAccelerationStructureKHR)
        {
            for (auto h : m_BLASHandles)
                vkDestroyAccelerationStructureKHR(device, h, nullptr);
        }
        for (const auto& mesh : m_Meshes)
            ResourceManager::Get().Release(MaterialHandle(mesh.materialIndex));
    }
}
} // namespace Chimera
