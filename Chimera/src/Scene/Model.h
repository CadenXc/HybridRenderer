#pragma once

#include "Scene/SceneCommon.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Resources/Buffer.h"
#include <vector>
#include <memory>

namespace Chimera {

    class Model 
    {
    public:
        Model(std::shared_ptr<VulkanContext> context, const ImportedScene& importedScene);
        ~Model();

        const std::vector<Mesh>& GetMeshes() const { return m_Meshes; }
        VkBuffer GetVertexBuffer() const { return m_VertexBuffer->GetBuffer(); }
        VkBuffer GetIndexBuffer() const { return m_IndexBuffer->GetBuffer(); }
        
        VkDeviceAddress GetVertexBufferAddress() const { return m_VertexBuffer->GetDeviceAddress(); }
        VkDeviceAddress GetIndexBufferAddress() const { return m_IndexBuffer->GetDeviceAddress(); }

        const std::vector<VkAccelerationStructureKHR>& GetBLASHandles() const { return m_BLASHandles; }

        uint32_t GetVertexCount() const { return m_VertexCount; }
        uint32_t GetIndexCount() const { return m_IndexCount; }

    private:
        void BuildBLAS();

    private:
        std::shared_ptr<VulkanContext> m_Context;
        std::unique_ptr<Buffer> m_VertexBuffer;
        std::unique_ptr<Buffer> m_IndexBuffer;
        std::vector<Mesh> m_Meshes;
        
        std::vector<std::unique_ptr<Buffer>> m_BLASBuffers;
        std::vector<VkAccelerationStructureKHR> m_BLASHandles;

        uint32_t m_VertexCount = 0;
        uint32_t m_IndexCount = 0;
    };
}
