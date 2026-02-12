#include "pch.h"
#include "Model.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Backend/RenderContext.h"
#include "Renderer/Resources/Buffer.h"
#include "Core/Log.h"

namespace Chimera
{
    Model::Model(std::shared_ptr<VulkanContext> context, const ImportedScene& importedScene)
        : m_Context(context), m_Meshes(importedScene.Meshes)
    {
        m_VertexCount = (uint32_t)importedScene.Vertices.size();
        m_IndexCount = (uint32_t)importedScene.Indices.size();

        CH_CORE_INFO("Model: Creating buffers for {0} vertices, {1} indices...", m_VertexCount, m_IndexCount);

        VkDeviceSize vertexBufferSize = sizeof(Vertex) * m_VertexCount;
        m_VertexBuffer = std::make_unique<Buffer>(
            vertexBufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );
        m_VertexBuffer->Update(importedScene.Vertices.data(), vertexBufferSize);

        VkDeviceSize indexBufferSize = sizeof(uint32_t) * m_IndexCount;
        m_IndexBuffer = std::make_unique<Buffer>(
            indexBufferSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );
        m_IndexBuffer->Update(importedScene.Indices.data(), indexBufferSize);

        if (VulkanContext::Get().IsRayTracingSupported())
        {
            BuildBLAS();
        }
    }

    Model::~Model()
    {
        if (m_Context)
        {
            VkDevice device = m_Context->GetDevice();
            auto vkDestroyAS = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureKHR");
            
            for (auto handle : m_BLASHandles)
            {
                if (handle != VK_NULL_HANDLE)
                {
                    vkDestroyAS(device, handle, nullptr);
                }
            }
        }
    }

    void Model::BuildBLAS()
    {
        CH_CORE_INFO("Model: Building {0} BLAS...", m_Meshes.size());
        m_BLASBuffers.clear();
        m_BLASHandles.clear();

        auto vkCreateAccelerationStructureKHR = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(VulkanContext::Get().GetDevice(), "vkCreateAccelerationStructureKHR");
        auto vkGetAccelerationStructureBuildSizesKHR = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(VulkanContext::Get().GetDevice(), "vkGetAccelerationStructureBuildSizesKHR");
        auto vkCmdBuildAccelerationStructuresKHR = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(VulkanContext::Get().GetDevice(), "vkCmdBuildAccelerationStructuresKHR");

        for (uint32_t i = 0; i < (uint32_t)m_Meshes.size(); i++)
        {
            auto& mesh = m_Meshes[i];

            VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
            geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
            geometry.geometry.triangles.vertexData.deviceAddress = m_VertexBuffer->GetDeviceAddress() + mesh.vertexOffset * sizeof(Vertex);
            geometry.geometry.triangles.vertexStride = sizeof(Vertex);
            geometry.geometry.triangles.maxVertex = m_VertexCount;
            geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
            geometry.geometry.triangles.indexData.deviceAddress = m_IndexBuffer->GetDeviceAddress() + mesh.indexOffset * sizeof(uint32_t);
            geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

            VkAccelerationStructureBuildGeometryInfoKHR buildInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
            buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
            buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
            buildInfo.geometryCount = 1;
            buildInfo.pGeometries = &geometry;

            uint32_t primitiveCount = mesh.indexCount / 3;
            VkAccelerationStructureBuildSizesInfoKHR sizeInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
            vkGetAccelerationStructureBuildSizesKHR(VulkanContext::Get().GetDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primitiveCount, &sizeInfo);

            auto blasBuffer = std::make_unique<Buffer>(
                sizeInfo.accelerationStructureSize,
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VMA_MEMORY_USAGE_GPU_ONLY
            );

            VkAccelerationStructureCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
            createInfo.buffer = (VkBuffer)(void*)(uintptr_t)blasBuffer->GetBuffer();
            createInfo.size = sizeInfo.accelerationStructureSize;
            createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

            VkAccelerationStructureKHR blas;
            vkCreateAccelerationStructureKHR(VulkanContext::Get().GetDevice(), &createInfo, nullptr, &blas);

            Buffer scratch(sizeInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

            buildInfo.dstAccelerationStructure = blas;
            buildInfo.scratchData.deviceAddress = scratch.GetDeviceAddress();

            VkAccelerationStructureBuildRangeInfoKHR rangeInfo{ primitiveCount, 0, 0, 0 };
            const VkAccelerationStructureBuildRangeInfoKHR* pRange = &rangeInfo;

            {
                ScopedCommandBuffer cmd;
                vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRange);
            }

            m_BLASBuffers.push_back(std::move(blasBuffer));
            m_BLASHandles.push_back(blas);
            CH_CORE_INFO("  BLAS {0} built. Primitives: {1}", i, primitiveCount);
        }
    }
}