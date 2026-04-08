#include "pch.h"
#include "LightManager.h"
#include "Scene/Scene.h"
#include "Scene/Model.h"
#include "Renderer/Resources/Buffer.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/Backend/VulkanContext.h"

namespace Chimera
{
static float TriangleArea(const glm::vec3& v0, const glm::vec3& v1,
                          const glm::vec3& v2)
{
    return glm::length(glm::cross(v1 - v0, v2 - v0)) * 0.5f;
}

LightManager::LightManager() {}

LightManager::~LightManager() {}

void LightManager::Build(Scene* scene)
{
    m_GpuLights.clear();
    m_LightsCDF.clear();

    if (!scene) return;

    const auto& entities = scene->GetEntities();
    uint32_t currentInstanceIdx = 0;

    for (const auto& entity : entities)
    {
        auto model = entity.mesh.model;
        if (!model || !model->IsReady())
        {
            // Even if not ready, we need to keep track of instance indices if
            // the renderer expects 1:1 mapping with entity meshes.
            // In Chimera, SyncInstancesToGPU flattens all meshes of all
            // entities.
            if (model)
                currentInstanceIdx += (uint32_t)model->GetMeshes().size();
            continue;
        }

        const auto& meshes = model->GetMeshes();
        glm::mat4 entityTransform = entity.transform.GetTransform();

        for (const auto& mesh : meshes)
        {
            uint32_t instanceIdx = currentInstanceIdx++;

            // Check if material is emissive
            Material* mat = ResourceManager::Get().GetMaterial(
                MaterialHandle(mesh.materialIndex));
            if (!mat || glm::length(mat->GetData().emission) < 0.001f) continue;
GpuLight light{};
light.instance = (int)instanceIdx;
light.environment = INVALID_ID;
light.cdfStart = (int)m_LightsCDF.size();
light.cdfCount = (uint32_t)mesh.indexCount / 3;

const auto& triangleData = model->GetTriangleData();

for (uint32_t i = 0; i < (uint32_t)light.cdfCount; ++i)
{
    // Each mesh starts at its own offset in the model's triangle array
    uint32_t triIdx = (mesh.indexOffset / 3) + i;
    if (triIdx >= triangleData.size()) break;

    const auto& tri = triangleData[triIdx];

    // Transform vertices to world space
    glm::vec3 v0 = glm::vec3(entityTransform * glm::vec4(glm::vec3(tri.positionUvX0), 1.0f));
    glm::vec3 v1 = glm::vec3(entityTransform * glm::vec4(glm::vec3(tri.positionUvX1), 1.0f));
    glm::vec3 v2 = glm::vec3(entityTransform * glm::vec4(glm::vec3(tri.positionUvX2), 1.0f));

    float area = TriangleArea(v0, v1, v2);
    m_LightsCDF.push_back(area + (m_LightsCDF.size() > (size_t)light.cdfStart ? m_LightsCDF.back() : 0.0f));
}

m_GpuLights.push_back(light);
        }
    }

    // Add environment light if exists
    uint32_t skyboxIdx = scene->GetSkyboxTextureIndex();
    if (skyboxIdx != 0xFFFFFFFF)
    {
        GpuLight light{};
        light.instance = INVALID_ID;
        light.environment = 0; // Assume first environment
        light.cdfStart = (int)m_LightsCDF.size();
        light.cdfCount = 0; // Environment sampling might use a different logic
                            // or pre-built texture CDF
        m_GpuLights.push_back(light);
    }

    // Sync to GPU
    if (!m_GpuLights.empty())
    {
        VkDeviceSize lightSize = m_GpuLights.size() * sizeof(GpuLight);
        if (!m_LightBuffer || m_LightBuffer->GetSize() < lightSize)
        {
            m_LightBuffer = std::make_unique<Buffer>(
                lightSize * 2,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU, "LightBuffer");
        }
        m_LightBuffer->Update(m_GpuLights.data(), lightSize);

        VkDeviceSize cdfSize = m_LightsCDF.size() * sizeof(float);
        if (!m_CDFBuffer || m_CDFBuffer->GetSize() < cdfSize)
        {
            m_CDFBuffer = std::make_unique<Buffer>(
                cdfSize * 2,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU, "CDFBuffer");
        }
        m_CDFBuffer->Update(m_LightsCDF.data(), cdfSize);
    }
}
} // namespace Chimera
