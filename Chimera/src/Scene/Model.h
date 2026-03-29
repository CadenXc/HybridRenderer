#pragma once

#include "Scene/SceneCommon.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Resources/Buffer.h"
#include <vector>
#include <memory>

namespace Chimera
{
    enum class LoadingStatus { Uninitialized, Loading, Uploading, Ready, Failed };

    class Model
    {
    public:
        // Regular constructor for immediate loading
        Model(std::shared_ptr<VulkanContext> context, const ImportedScene &importedScene);
        
        // Constructor for async loading: creates a placeholder
        Model(std::shared_ptr<VulkanContext> context);
        
        ~Model();

        LoadingStatus GetStatus() const { return m_Status; }
        bool IsReady() const { return m_Status == LoadingStatus::Ready; }
        
        // Called by main thread when CPU parsing is done to upload to GPU
        void UploadToGPU(const ImportedScene& sceneData);

        const std::vector<Mesh> &GetMeshes() const
        {
            return m_Meshes;
        }

        Buffer *GetVertexBuffer() const
        {
            return m_VertexBuffer.get();
        }

        Buffer *GetIndexBuffer() const
        {
            return m_IndexBuffer.get();
        }

        VkDeviceAddress GetVertexBufferAddress() const
        {
            return m_VertexBuffer->GetDeviceAddress();
        }

        VkDeviceAddress GetIndexBufferAddress() const
        {
            return m_IndexBuffer->GetDeviceAddress();
        }

        const std::vector<VkAccelerationStructureKHR> &GetBLASHandles() const
        {
            return m_BLASHandles;
        }

        uint32_t GetVertexCount() const
        {
            return m_VertexCount;
        }

        uint32_t GetIndexCount() const
        {
            return m_IndexCount;
        }

        void Draw(class GraphicsExecutionContext &ctx);

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

        LoadingStatus m_Status = LoadingStatus::Uninitialized;
    };
}