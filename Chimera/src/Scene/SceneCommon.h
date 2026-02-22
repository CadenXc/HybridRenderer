#pragma once

#include "pch.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <vector>
#include <array>

#include "Renderer/Resources/ResourceHandle.h"
#include "Renderer/Backend/ShaderCommon.h"

namespace Chimera
{

    struct VertexInfo : public GpuVertex
    {
        bool operator==(const VertexInfo& other) const
        {
            return pos == other.pos && normal == other.normal && texCoord == other.texCoord;
        }

        static VkVertexInputBindingDescription getBindingDescription()
        {
            VkVertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(GpuVertex);
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            return bindingDescription;
        }

        static std::array<VkVertexInputAttributeDescription, 4> getAttributeDescriptions()
        {
            std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{};
            attributeDescriptions[0].binding = 0;
            attributeDescriptions[0].location = 0;
            attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[0].offset = offsetof(GpuVertex, pos); // 0

            attributeDescriptions[1].binding = 0;
            attributeDescriptions[1].location = 1;
            attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[1].offset = offsetof(GpuVertex, normal); // 12

            attributeDescriptions[2].binding = 0;
            attributeDescriptions[2].location = 2;
            attributeDescriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attributeDescriptions[2].offset = offsetof(GpuVertex, tangent); // 24

            attributeDescriptions[3].binding = 0;
            attributeDescriptions[3].location = 3;
            attributeDescriptions[3].format = VK_FORMAT_R32G32_SFLOAT;
            attributeDescriptions[3].offset = offsetof(GpuVertex, texCoord); // 40

            return attributeDescriptions;
        }
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

    class Model;

    struct TransformComponent
    {
        glm::vec3 position{ 0.0f };
        glm::vec3 rotation{ 0.0f }; // Euler angles in degrees
        glm::vec3 scale{ 1.0f };
        
        glm::mat4 GetTransform() const
        {
            glm::mat4 trs = glm::translate(glm::mat4(1.0f), position);
            trs = glm::rotate(trs, glm::radians(rotation.x), { 1, 0, 0 });
            trs = glm::rotate(trs, glm::radians(rotation.y), { 0, 1, 0 });
            trs = glm::rotate(trs, glm::radians(rotation.z), { 0, 0, 1 });
            return glm::scale(trs, scale);
        }
    };

    struct MeshComponent
    {
        std::shared_ptr<Model> model;
        MaterialRef material;
    };

    struct Entity
    {
        std::string name;
        TransformComponent transform;
        glm::mat4 prevTransform{ 1.0f }; // [NEW]
        MeshComponent mesh;
    };

    // 存储 Importer 产出的结果
    struct ImportedScene
    {
        std::vector<VertexInfo> Vertices;
        std::vector<uint32_t> Indices;
        std::vector<Mesh> Meshes;
        std::vector<GpuMaterial> Materials;
        std::vector<Node> Nodes;
    };
}

namespace std
{
    template<> struct hash<Chimera::VertexInfo>
    {
        size_t operator()(const Chimera::VertexInfo& vertex) const
        {
            return ((hash<glm::vec3>()(vertex.pos) ^ (hash<glm::vec3>()(vertex.normal) << 1)) >> 1) ^ (hash<glm::vec2>()(vertex.texCoord) << 1);
        }
    };
}
