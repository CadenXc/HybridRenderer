#pragma once

#include "pch.h"
#include "gfx/vulkan/VulkanContext.h"
#include "gfx/resources/Buffer.h"
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include <array>

namespace Chimera {

    struct Vertex {
        glm::vec3 pos;
        glm::vec3 normal;
        glm::vec4 tangent;
        glm::vec2 texCoord;

        bool operator==(const Vertex& other) const {
            return pos == other.pos && normal == other.normal && tangent == other.tangent && texCoord == other.texCoord;
        }

        static VkVertexInputBindingDescription getBindingDescription() {
            VkVertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(Vertex);
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            return bindingDescription;
        }

        static std::array<VkVertexInputAttributeDescription, 4> getAttributeDescriptions() {
            std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{};
            attributeDescriptions[0].binding = 0;
            attributeDescriptions[0].location = 0;
            attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[0].offset = offsetof(Vertex, pos);

            attributeDescriptions[1].binding = 0;
            attributeDescriptions[1].location = 1;
            attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[1].offset = offsetof(Vertex, normal);

            attributeDescriptions[2].binding = 0;
            attributeDescriptions[2].location = 2;
            attributeDescriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attributeDescriptions[2].offset = offsetof(Vertex, tangent);

            attributeDescriptions[3].binding = 0;
            attributeDescriptions[3].location = 3;
            attributeDescriptions[3].format = VK_FORMAT_R32G32_SFLOAT;
            attributeDescriptions[3].offset = offsetof(Vertex, texCoord);
            return attributeDescriptions;
        }
    };
}

namespace std {
    template<> struct hash<Chimera::Vertex> {
        size_t operator()(Chimera::Vertex const& vertex) const {
            return ((hash<glm::vec3>()(vertex.pos) ^ 
                   (hash<glm::vec3>()(vertex.normal) << 1)) >> 1) ^ 
                   (hash<glm::vec4>()(vertex.tangent) << 1) ^
                   (hash<glm::vec2>()(vertex.texCoord) << 1);
        }
    };
}

namespace Chimera {

    struct Camera {
        glm::mat4 view{ 1.0f };
        glm::mat4 proj{ 1.0f };
        glm::mat4 viewInverse{ 1.0f };
        glm::mat4 projInverse{ 1.0f };
    };

    struct DirectionalLight {
        glm::vec4 direction{ 0.0f, -1.0f, 0.0f, 0.0f };
        glm::vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec4 position{ 0.0f, 0.0f, 0.0f, 1.0f }; // For simple point light behavior if needed
    };

    class Scene {
    public:
        Scene(std::shared_ptr<VulkanContext> context);
        ~Scene();

        void LoadModel(const std::string& path);

        // Getters for rendering
        Buffer* GetVertexBuffer() const { return m_VertexBuffer.get(); }
        Buffer* GetIndexBuffer() const { return m_IndexBuffer.get(); }
        uint32_t GetIndexCount() const { return static_cast<uint32_t>(m_Indices.size()); }
        uint32_t GetVertexCount() const { return static_cast<uint32_t>(m_Vertices.size()); }
        
        const std::vector<Vertex>& GetVertices() const { return m_Vertices; }
        const std::vector<uint32_t>& GetIndices() const { return m_Indices; }

        void BuildBLAS();
        VkAccelerationStructureKHR GetBLAS() const { return m_BottomLevelAS; }

        void BuildTLAS();
        VkAccelerationStructureKHR GetTLAS() const { return m_TopLevelAS; }

        // Camera & Light
        Camera& GetCamera() { return m_Camera; }
        const Camera& GetCamera() const { return m_Camera; }
        
        DirectionalLight& GetLight() { return m_Light; }
        const DirectionalLight& GetLight() const { return m_Light; }

    private:
        void LoadObj(const std::string& path);
        void LoadGLTF(const std::string& path);
        void CreateVertexBuffer();
        void CreateIndexBuffer();
        void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
        VkCommandBuffer BeginSingleTimeCommands();
        void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

    private:
        std::shared_ptr<VulkanContext> m_Context;

        std::vector<Vertex> m_Vertices;
        std::vector<uint32_t> m_Indices;

        std::unique_ptr<Buffer> m_VertexBuffer;
        std::unique_ptr<Buffer> m_IndexBuffer;

        VkAccelerationStructureKHR m_BottomLevelAS;
        std::unique_ptr<Buffer> m_BLASBuffer;

        VkAccelerationStructureKHR m_TopLevelAS = VK_NULL_HANDLE;
        std::unique_ptr<Buffer> m_TLASBuffer;

        Camera m_Camera;
        DirectionalLight m_Light;
    };

}


