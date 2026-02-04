#pragma once

#include "pch.h"
#include "Scene/SceneCommon.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Resources/Buffer.h"
#include "Renderer/Resources/ResourceManager.h"
#include <unordered_map>
#include <vector>
#include <memory>

namespace Chimera
{
    class Scene
    {
    public:
        Scene(std::shared_ptr<VulkanContext> context, ResourceManager* resourceManager);
        ~Scene();

        void LoadModel(const std::string& path);

        const std::vector<Mesh>& GetMeshes() const { return m_Meshes; }
        const std::vector<Material>& GetMaterials() const { return m_Materials; }
        
        VkBuffer GetVertexBuffer() const { return m_VertexBuffer->GetBuffer(); }
        VkBuffer GetIndexBuffer() const { return m_IndexBuffer->GetBuffer(); }
        
        uint32_t GetVertexCount() const { return (uint32_t)m_VertexCount; }
        uint32_t GetIndexCount() const { return (uint32_t)m_IndexCount; }

        VkDeviceAddress GetVertexBufferAddress() const { return m_VertexBuffer->GetDeviceAddress(); }
        VkDeviceAddress GetIndexBufferAddress() const { return m_IndexBuffer->GetDeviceAddress(); }

        VkAccelerationStructureKHR GetTLAS() const { return m_TopLevelAS; }

        Camera& GetCamera() { return m_Camera; }
        DirectionalLight& GetLight() { return m_Light; }

    private:
        void CreateVertexBuffer(const std::vector<Vertex>& vertices);
        void CreateIndexBuffer(const std::vector<uint32_t>& indices);
        void BuildBLAS();
        void BuildTLAS();

        void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

    private:
        std::shared_ptr<VulkanContext> m_Context;
        ResourceManager* m_ResourceManager = nullptr;

        std::unique_ptr<Buffer> m_VertexBuffer;
        std::unique_ptr<Buffer> m_IndexBuffer;
        size_t m_VertexCount = 0;
        size_t m_IndexCount = 0;

        std::vector<Mesh> m_Meshes;
        std::vector<Material> m_Materials;

        std::vector<std::unique_ptr<Buffer>> m_BLASBuffers;
        std::vector<VkAccelerationStructureKHR> m_BLASHandles;
        
        VkAccelerationStructureKHR m_TopLevelAS = VK_NULL_HANDLE;
        std::unique_ptr<Buffer> m_TLASBuffer;

        Camera m_Camera;
        DirectionalLight m_Light;
        
        // 我们以后会将纹理管理进一步移交给 ResourceManager，目前保留在 Scene 用于展示
        std::vector<std::unique_ptr<Image>> m_LoadedTextures;
        std::unordered_map<std::string, int> m_TextureMap;
    };
}
