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

struct ChimeraAABB
{
    glm::vec3 min{std::numeric_limits<float>::max()};
    glm::vec3 max{std::numeric_limits<float>::lowest()};

    ChimeraAABB() = default;
    ChimeraAABB(const glm::vec3& min, const glm::vec3& max) : min(min), max(max) {}

    void Merge(const glm::vec3& point)
    {
        min = glm::min(min, point);
        max = glm::max(max, point);
    }

    void Merge(const ChimeraAABB& other)
    {
        min = glm::min(min, other.min);
        max = glm::max(max, other.max);
    }

    bool IsValid() const
    {
        return min.x <= max.x && min.y <= max.y && min.z <= max.z;
    }

    glm::vec3 GetCenter() const
    {
        return (min + max) * 0.5f;
    }
    glm::vec3 GetExtent() const
    {
        return (max - min) * 0.5f;
    }

    ChimeraAABB Transform(const glm::mat4& transform) const
    {
        if (!IsValid()) return *this;

        glm::vec3 corners[8] = {{min.x, min.y, min.z}, {max.x, min.y, min.z},
                                {min.x, max.y, min.z}, {max.x, max.y, min.z},
                                {min.x, min.y, max.z}, {max.x, min.y, max.z},
                                {min.x, max.y, max.z}, {max.x, max.y, max.z}};

        ChimeraAABB result;
        for (int i = 0; i < 8; ++i)
            result.Merge(glm::vec3(transform * glm::vec4(corners[i], 1.0f)));
        return result;
    }

    GpuAABB ToGpuAABB() const
    {
        GpuAABB g;
        g.min = min;
        g.max = max;
        g.pad0 = 0.0f;
        g.pad1 = 0.0f;
        return g;
    }
};

struct Plane
{
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float distance{0.0f};

    Plane() = default;
    Plane(const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3)
    {
        auto edge1 = p2 - p1;
        auto edge2 = p3 - p1;
        normal = glm::normalize(glm::cross(edge1, edge2));
        distance = glm::dot(normal, p1);
    }

    float GetSignedDistance(const glm::vec3& point) const
    {
        return glm::dot(normal, point) - distance;
    }
};

struct Frustum
{
    std::array<Plane, 6> planes;

    bool Intersects(const ChimeraAABB& aabb) const
    {
        if (!aabb.IsValid()) return false;

        for (const auto& plane : planes)
        {
            glm::vec3 p = aabb.min;
            if (plane.normal.x >= 0) p.x = aabb.max.x;
            if (plane.normal.y >= 0) p.y = aabb.max.y;
            if (plane.normal.z >= 0) p.z = aabb.max.z;

            if (plane.GetSignedDistance(p) < 0) return false;
        }
        return true;
    }

    static Frustum FromViewProj(const glm::mat4& vp)
    {
        Frustum frustum;
        frustum.planes[0].normal.x = vp[0][3] + vp[0][0];
        frustum.planes[0].normal.y = vp[1][3] + vp[1][0];
        frustum.planes[0].normal.z = vp[2][3] + vp[2][0];
        frustum.planes[0].distance = -(vp[3][3] + vp[3][0]);

        frustum.planes[1].normal.x = vp[0][3] - vp[0][0];
        frustum.planes[1].normal.y = vp[1][3] - vp[1][0];
        frustum.planes[1].normal.z = vp[2][3] - vp[2][0];
        frustum.planes[1].distance = -(vp[3][3] - vp[3][0]);

        frustum.planes[2].normal.x = vp[0][3] + vp[0][1];
        frustum.planes[2].normal.y = vp[1][3] + vp[1][1];
        frustum.planes[2].normal.z = vp[2][3] + vp[2][1];
        frustum.planes[2].distance = -(vp[3][3] + vp[3][1]);

        frustum.planes[3].normal.x = vp[0][3] - vp[0][1];
        frustum.planes[3].normal.y = vp[1][3] - vp[1][1];
        frustum.planes[3].normal.z = vp[2][3] - vp[2][1];
        frustum.planes[3].distance = -(vp[3][3] - vp[3][1]);

        frustum.planes[4].normal.x = vp[0][2];
        frustum.planes[4].normal.y = vp[1][2];
        frustum.planes[4].normal.z = vp[2][2];
        frustum.planes[4].distance = -vp[3][2];

        frustum.planes[5].normal.x = vp[0][3] - vp[0][2];
        frustum.planes[5].normal.y = vp[1][3] - vp[1][2];
        frustum.planes[5].normal.z = vp[2][3] - vp[2][2];
        frustum.planes[5].distance = -(vp[3][3] - vp[3][2]);

        for (auto& plane : frustum.planes)
        {
            float length = glm::length(plane.normal);
            plane.normal /= length;
            plane.distance /= length;
        }

        return frustum;
    }
};

struct OctreeNode
{
    ChimeraAABB bounds;
    std::vector<uint32_t> entityIndices;
    std::unique_ptr<OctreeNode> children[8];
    bool isLeaf = true;

    OctreeNode(const ChimeraAABB& b) : bounds(b) {}
};

enum class LightType
{
    Directional = 0,
    Point = 1,
    Spot = 2
};

struct Light
{
    glm::vec4 position;
    glm::vec4 color;
    glm::vec4 direction;
};

struct VertexInfo : public GpuVertex
{
    static VkVertexInputBindingDescription getBindingDescription()
    {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(GpuVertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 4>
    getAttributeDescriptions()
    {
        std::array<VkVertexInputAttributeDescription, 4>
            attributeDescriptions{};
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(GpuVertex, pos);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(GpuVertex, normal);

        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(GpuVertex, tangent);

        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[3].offset = offsetof(GpuVertex, texCoord);

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
    glm::mat4 transform{1.0f};
    ChimeraAABB localBounds;
};

struct Node
{
    std::string name;
    glm::mat4 transform{1.0f};
    int meshIndex = -1;
    std::vector<int> children;
};

class Model;

struct TransformComponent
{
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f};
    glm::vec3 scale{1.0f};

    glm::mat4 GetTransform() const
    {
        glm::mat4 trs = glm::translate(glm::mat4(1.0f), position);
        trs = glm::rotate(trs, glm::radians(rotation.x), {1, 0, 0});
        trs = glm::rotate(trs, glm::radians(rotation.y), {0, 1, 0});
        trs = glm::rotate(trs, glm::radians(rotation.z), {0, 0, 1});
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
    glm::mat4 prevTransform{1.0f};
    MeshComponent mesh;
    uint32_t rootNodeIndex = 0;
    uint32_t primitiveOffset = 0;
};

struct ImportedScene
{
    std::vector<VertexInfo> Vertices;
    std::vector<uint32_t> Indices;
    std::vector<Mesh> Meshes;
    std::vector<GpuMaterial> Materials;
    std::vector<Node> Nodes;
    std::vector<GpuTriangle> Triangles;
};

struct IndexData
{
    uint32_t triangleDataStartInx;
    uint32_t indicesDataStartInx;
    uint32_t bvhNodeDataStartInx;
    uint32_t triangleCount;
};

} // namespace Chimera
