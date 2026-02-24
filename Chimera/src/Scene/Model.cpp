#include "pch.h"
#include "Model.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Backend/RenderContext.h"
#include "Renderer/Resources/Buffer.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"
#include "Core/Log.h"

namespace Chimera
{

    Model::Model(std::shared_ptr<VulkanContext> context, const ImportedScene& importedScene)
        : m_Context(context), m_Meshes(importedScene.Meshes)
    {
        m_VertexCount = (uint32_t)importedScene.Vertices.size();
        m_IndexCount = (uint32_t)importedScene.Indices.size();

        CH_CORE_INFO("Model: Creating buffers for {0} vertices, {1} indices...", m_VertexCount, m_IndexCount);

        VkDeviceSize vertexBufferSize = sizeof(GpuVertex) * m_VertexCount;
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

        BuildBLAS();
    }

    Model::~Model()
    {
        if (m_Context && !m_BLASHandles.empty())
        {
            VkDevice device = m_Context->GetDevice();
            for (auto handle : m_BLASHandles)
            {
                if (handle != VK_NULL_HANDLE)
                {
                    vkDestroyAccelerationStructureKHR(device, handle, nullptr);
                }
            }
        }
        m_BLASHandles.clear();
        m_BLASBuffers.clear();
    }

    void Model::Draw(GraphicsExecutionContext& ctx)
    {
        VkBuffer vBuffer = (VkBuffer)m_VertexBuffer->GetBuffer();
        VkDeviceSize offset = 0;
        ctx.BindVertexBuffers(0, 1, &vBuffer, &offset);
        ctx.BindIndexBuffer((VkBuffer)m_IndexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

        for (const auto& mesh : m_Meshes)
        {
            ctx.DrawIndexed(mesh.indexCount, 1, mesh.indexOffset, (int32_t)mesh.vertexOffset, 0);
        }
    }

    void Model::BuildBLAS()
    {
        VkDevice device = m_Context->GetDevice();
        CH_CORE_INFO("Model: Building {0} BLAS using volk dispatch...", m_Meshes.size());

        m_BLASHandles.clear();
        m_BLASBuffers.clear();

        for (uint32_t i = 0; i < (uint32_t)m_Meshes.size(); ++i)
        {
            const auto& mesh = m_Meshes[i];
            if (mesh.indexCount == 0) continue;

            VkAccelerationStructureGeometryKHR geom{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
            geom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            geom.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            geom.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
            geom.geometry.triangles.vertexData.deviceAddress = m_VertexBuffer->GetDeviceAddress();
            geom.geometry.triangles.vertexStride = sizeof(GpuVertex);
            geom.geometry.triangles.maxVertex = m_VertexCount;
            geom.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
            geom.geometry.triangles.indexData.deviceAddress = m_IndexBuffer->GetDeviceAddress();
            geom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

            VkAccelerationStructureBuildGeometryInfoKHR buildInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
            buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
            buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
            buildInfo.geometryCount = 1;
            buildInfo.pGeometries = &geom;

            uint32_t maxPrimitiveCount = mesh.indexCount / 3;
            VkAccelerationStructureBuildSizesInfoKHR sizeInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
            
            vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &maxPrimitiveCount, &sizeInfo);

            auto blasBuffer = std::make_unique<Buffer>(sizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
            
            VkAccelerationStructureCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
            createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            createInfo.buffer = (VkBuffer)blasBuffer->GetBuffer();
            createInfo.size = sizeInfo.accelerationStructureSize;

            VkAccelerationStructureKHR handle;
            if (vkCreateAccelerationStructureKHR(device, &createInfo, nullptr, &handle) != VK_SUCCESS)
            {
                CH_CORE_ERROR("  [BLAS {0}] FAILED to create AS handle!", i);
                continue;
            }

            Buffer scratch(sizeInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
            buildInfo.dstAccelerationStructure = handle;
            buildInfo.scratchData.deviceAddress = scratch.GetDeviceAddress();

            VkAccelerationStructureBuildRangeInfoKHR rangeInfo{ maxPrimitiveCount, mesh.indexOffset * sizeof(uint32_t), (int32_t)mesh.vertexOffset, 0 };
            const VkAccelerationStructureBuildRangeInfoKHR* pRange = &rangeInfo;

            {
                ScopedCommandBuffer cmd;
                vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRange);
            }

            m_BLASBuffers.push_back(std::move(blasBuffer));
            m_BLASHandles.push_back(handle);
        }
    }
}
