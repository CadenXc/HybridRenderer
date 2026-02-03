#pragma once

#include "pch.h"
#include "gfx/vulkan/VulkanContext.h"
#include "gfx/resources/Buffer.h"
#include "gfx/resources/ResourceManager.h"
#include <unordered_map>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include <array>

namespace Chimera
{
    struct Vertex
    {
        glm::vec3 pos;
        glm::vec3 normal;
        glm::vec4 tangent;
        glm::vec2 texCoord;

        bool operator==(const Vertex& other) const
        {
            return pos == other.pos && normal == other.normal && tangent == other.tangent && texCoord == other.texCoord;
        }

        static VkVertexInputBindingDescription getBindingDescription()
        {
            VkVertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(Vertex);
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            return bindingDescription;
        }

        static std::array<VkVertexInputAttributeDescription, 4> getAttributeDescriptions()
        {
            std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{};
            // Location 0: Pos
            attributeDescriptions[0].binding = 0;
            attributeDescriptions[0].location = 0;
            attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[0].offset = offsetof(Vertex, pos);
            // Location 1: Normal
            attributeDescriptions[1].binding = 0;
            attributeDescriptions[1].location = 1;
            attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[1].offset = offsetof(Vertex, normal);
            // Location 2: Tangent
            attributeDescriptions[2].binding = 0;
            attributeDescriptions[2].location = 2;
            attributeDescriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attributeDescriptions[2].offset = offsetof(Vertex, tangent);
            // Location 3: UV
            attributeDescriptions[3].binding = 0;
            attributeDescriptions[3].location = 3;
            attributeDescriptions[3].format = VK_FORMAT_R32G32_SFLOAT;
            attributeDescriptions[3].offset = offsetof(Vertex, texCoord);
            return attributeDescriptions;
        }
    };

    struct Material
    {
        glm::vec4 baseColor{ 1.0f };
        glm::vec4 emission{ 0.0f };
        float metallic{ 0.0f };
        float roughness{ 0.5f };
        float alphaCutoff{ 0.5f };
        int alphaMask{ 0 };
        
        int base_color_texture = -1;
        int normal_map = -1;
        int metallic_roughness_map = -1;
        int emissive_map = -1;
    };

    struct Mesh
    {
        std::string name;
        uint32_t indexCount = 0;
        uint32_t indexOffset = 0;
        uint32_t vertexOffset = 0;
        int materialIndex = 0;
        glm::mat4 transform{ 1.0f };
    };

    struct Node 
    {
        std::string name;
        glm::mat4 transform{ 1.0f };
        int meshIndex = -1;
        std::vector<int> children;
    };

    struct Camera
    {
        glm::mat4 view{ 1.0f };
        glm::mat4 proj{ 1.0f };
        glm::mat4 viewInverse{ 1.0f };
        glm::mat4 projInverse{ 1.0f };
    };

    struct DirectionalLight
    {
        glm::vec4 direction;
        glm::vec4 color; 
    };

    class Scene
    {
    public:
        Scene(std::shared_ptr<VulkanContext> context, ResourceManager* resourceManager);
        ~Scene();

        void LoadModel(const std::string& path);

        const std::vector<Mesh>& GetMeshes() const { return m_Meshes; }
        const std::vector<Material>& GetMaterials() const { return m_Materials; }
        
        // 核心 Buffer 获取
        VkBuffer GetVertexBuffer() const { return m_VertexBuffer->GetBuffer(); }
        VkBuffer GetIndexBuffer() const { return m_IndexBuffer->GetBuffer(); }
        
        // [新增] 兼容性接口：获取数量
        uint32_t GetVertexCount() const { return (uint32_t)m_Vertices.size(); }
        uint32_t GetIndexCount() const { return (uint32_t)m_Indices.size(); }

        // [新增] 兼容性接口：为光追路径提供 Buffer 地址
        VkDeviceAddress GetVertexBufferAddress() const { return m_VertexBuffer->GetDeviceAddress(); }
        VkDeviceAddress GetIndexBufferAddress() const { return m_IndexBuffer->GetDeviceAddress(); }

        VkAccelerationStructureKHR GetTLAS() const { return m_TopLevelAS; }

        Camera& GetCamera() { return m_Camera; }
        DirectionalLight& GetLight() { return m_Light; }

    private:
        void LoadGLTF(const std::string& path);
        void CreateVertexBuffer();
        void CreateIndexBuffer();
        void BuildBLAS();
        void BuildTLAS();

        void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
        VkCommandBuffer BeginSingleTimeCommands();
        void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

    private:
        std::shared_ptr<VulkanContext> m_Context;
        ResourceManager* m_ResourceManager = nullptr;

        std::vector<Vertex> m_Vertices;
        std::vector<uint32_t> m_Indices;
        std::unique_ptr<Buffer> m_VertexBuffer;
        std::unique_ptr<Buffer> m_IndexBuffer;

        std::vector<Node> m_Nodes;
        std::vector<Mesh> m_Meshes;
        std::vector<Material> m_Materials;

        std::vector<std::unique_ptr<Buffer>> m_BLASBuffers;
        std::vector<VkAccelerationStructureKHR> m_BLASHandles;
        
        VkAccelerationStructureKHR m_TopLevelAS = VK_NULL_HANDLE;
        std::unique_ptr<Buffer> m_TLASBuffer;

        Camera m_Camera;
        DirectionalLight m_Light;
        
        std::vector<std::unique_ptr<Image>> m_LoadedTextures;
        std::unordered_map<std::string, int> m_TextureMap;
    };
}

namespace std 
{
    template<> struct hash<Chimera::Vertex>
    {
        size_t operator()(const Chimera::Vertex& vertex) const
        {
            return ((hash<glm::vec3>()(vertex.pos) ^ (hash<glm::vec3>()(vertex.normal) << 1)) >> 1) ^ (hash<glm::vec2>()(vertex.texCoord) << 1);
        }
    };
}