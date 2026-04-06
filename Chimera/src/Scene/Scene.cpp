#include "pch.h"
#include "Scene.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/Backend/RenderContext.h"
#include "Assets/AssetImporter.h"
#include "Model.h"
#include "Core/Application.h"
#include "Renderer/Pipelines/RenderPath.h"
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>

namespace Chimera
{

Scene::Scene(std::shared_ptr<VulkanContext> context) : m_Context(context.get())
{
}

Scene::~Scene()
{
    CH_CORE_INFO("Scene: Destructor CALLED.");
    if (m_Context)
    {
        VkDevice device = m_Context->GetDevice();
        if (device != VK_NULL_HANDLE)
        {
            if (m_TopLevelAS != VK_NULL_HANDLE)
            {
                if (vkDestroyAccelerationStructureKHR)
                {
                    vkDestroyAccelerationStructureKHR(device, m_TopLevelAS,
                                                      nullptr);
                }
                m_TopLevelAS = VK_NULL_HANDLE;
            }
        }

        m_TLASBuffer.reset();
        m_ASInstanceBuffer.reset();
    }
    CH_CORE_INFO("Scene: Destructor FINISHED.");
}

void Scene::LoadModel(const std::string& path)
{
        // Now just fire the request and return immediately!
    ResourceManager::Get().LoadModelAsync(
        path, Application::Get().GetActiveSceneShared());
}

void Scene::FinalizeAsyncModelLoad(std::shared_ptr<Model> model,
                                   std::shared_ptr<ImportedScene> imported,
                                   const std::string& path)
{
    if (!imported) return;

        // 1. Sync Materials
    std::vector<uint32_t> globalMatIndices;
    for (const auto& gpuMat : imported->Materials)
    {
        auto mat = std::make_unique<Material>();
        mat->SetData(gpuMat);
        MaterialHandle h = ResourceManager::Get().AddMaterial(std::move(mat));
        globalMatIndices.push_back(h.id);
    }

    for (auto& mesh : imported->Meshes)
    {
        if (mesh.materialIndex < (int)globalMatIndices.size())
        {
            mesh.materialIndex = globalMatIndices[mesh.materialIndex];
        }
    }
    ResourceManager::Get().SyncMaterialsToGPU();

        // 2. Import Nodes (Hierarchy)
    uint32_t modelRootIdx = (uint32_t)m_Nodes.size();
    Node modelRoot{};
    modelRoot.name = std::filesystem::path(path).filename().string();
    modelRoot.transform = glm::mat4(1.0f);
    m_Nodes.push_back(modelRoot);

    uint32_t nodeOffset = (uint32_t)m_Nodes.size();
    for (const auto& node : imported->Nodes)
    {
        Node n = node;
        for (auto& child : n.children) child += nodeOffset;
        m_Nodes.push_back(n);
    }
    m_Nodes[modelRootIdx].children.push_back(nodeOffset);

        // 3. Create Entity and Link
    Entity entity{};
    entity.name = modelRoot.name;
    entity.mesh.model = model;
    entity.rootNodeIndex = modelRootIdx;
    entity.transform.position = {0, 0, 0};
    entity.prevTransform = entity.transform.GetTransform();

    uint32_t currentOffset = 0;
    for (const auto& e : m_Entities)
    {
        if (e.mesh.model)
            currentOffset += (uint32_t)e.mesh.model->GetMeshes().size();
    }
    entity.primitiveOffset = currentOffset;

    m_Entities.push_back(entity);

    m_WorldTransforms.resize(m_Nodes.size(), glm::mat4(1.0f));
    UpdateWorldTransforms();
    BuildOctree();
    UpdateTLAS();

    if (auto* renderPath = Application::Get().GetActiveRenderPath())
    {
        renderPath->OnSceneUpdated();
    }

    CH_CORE_INFO("Scene: Async model {0} integrated successfully.", path);
}

void Scene::UpdateWorldTransforms()
{
    if (m_Entities.empty() || m_Nodes.empty()) return;

    if (m_WorldTransforms.size() != m_Nodes.size())
        m_WorldTransforms.resize(m_Nodes.size(), glm::mat4(1.0f));

    for (const auto& entity : m_Entities)
    {
        if (entity.rootNodeIndex < m_Nodes.size())
        {
            ComputeWorldTransform(entity.rootNodeIndex,
                                  entity.transform.GetTransform());
        }
    }
}

void Scene::ComputeWorldTransform(uint32_t nodeIndex,
                                  const glm::mat4& parentTransform)
{
    if (nodeIndex >= m_Nodes.size()) return;

    Node& node = m_Nodes[nodeIndex];
    m_WorldTransforms[nodeIndex] = parentTransform * node.transform;

    for (uint32_t childIdx : node.children)
    {
        ComputeWorldTransform(childIdx, m_WorldTransforms[nodeIndex]);
    }
}

void Scene::RemoveEntity(uint32_t index)
{
    if (index < m_Entities.size())
    {
        m_EntitiesToRemove.push_back(index);
    }
}

void Scene::OnUpdate(float ts)
{
    if (!m_EntitiesToRemove.empty())
    {
        // Sort in descending order to avoid index shifting problems during
        // erasure
        std::sort(m_EntitiesToRemove.begin(), m_EntitiesToRemove.end(),
                  std::greater<uint32_t>());

        for (uint32_t index : m_EntitiesToRemove)
        {
            if (index >= m_Entities.size()) continue;

            auto& entity = m_Entities[index];
            CH_CORE_INFO("Scene: Deferring removal of entity '{}'.",
                         entity.name);

            // Capture the shared_ptr to the model
            std::shared_ptr<Model> modelToDestroy = entity.mesh.model;

            // Move the actual deletion to the resource manager's queue (safe
            // after GPU is done)
            ResourceManager::SubmitResourceFree(
                [modelToDestroy]() mutable
                {
                    modelToDestroy
                        .reset(); // This triggers Model destructor later
                });

            m_Entities.erase(m_Entities.begin() + index);
        }
        m_EntitiesToRemove.clear();

        // 1. Recalculate all primitive offsets to maintain sync with GPU/TLAS
        uint32_t currentOffset = 0;
        for (auto& e : m_Entities)
        {
            e.primitiveOffset = currentOffset;
            if (e.mesh.model)
                currentOffset += (uint32_t)e.mesh.model->GetMeshes().size();
        }

        // 2. Rebuild auxiliary structures
        BuildOctree();
        MarkDirty(); // Rebuild TLAS
    }

    UpdateWorldTransforms();

    if (m_NeedsTLASRebuild)
    {
        UpdateTLAS();
        m_NeedsTLASRebuild = false;
    }

    if (m_NeedsMaterialSync)
    {
        ResourceManager::Get().SyncMaterialsToGPU();
        m_NeedsMaterialSync = false;
    }
}

void Scene::UpdateEntityTRS(uint32_t index, const glm::vec3& pos,
                            const glm::vec3& rot, const glm::vec3& scale)
{
    if (index < m_Entities.size())
    {
        auto& e = m_Entities[index];
        e.prevTransform = e.transform.GetTransform();

        e.transform.position = pos;
        e.transform.rotation = rot;
        e.transform.scale = scale;

        MarkDirty();
    }
}

void Scene::ClearScene()
{
    CH_CORE_INFO("Scene: ClearScene() started.");
    m_Entities.clear();
    m_Nodes.clear();
    m_WorldTransforms.clear();
    m_OctreeRoot.reset();
    UpdateTLAS();
    CH_CORE_INFO("Scene: ClearScene() finished.");
}

void Scene::BuildOctree()
{
    m_OctreeRoot.reset(); // Always clear old tree first
    if (m_Entities.empty()) return;

    // 1. Calculate Scene Bounds
    AABB sceneBounds;
    for (const auto& entity : m_Entities)
    {
        if (!entity.mesh.model) continue;
        glm::mat4 entityTransform = entity.transform.GetTransform();
        for (const auto& mesh : entity.mesh.model->GetMeshes())
        {
            sceneBounds.Merge(
                mesh.localBounds.Transform(entityTransform * mesh.transform));
        }
    }

    // 2. Initialize Root
    m_OctreeRoot = std::make_unique<OctreeNode>(sceneBounds);
    for (uint32_t i = 0; i < (uint32_t)m_Entities.size(); ++i)
    {
        m_OctreeRoot->entityIndices.push_back(i);
    }

    // 3. Recursive Subdivide
    SubdivideOctree(m_OctreeRoot.get(), 0);
}

void Scene::SubdivideOctree(OctreeNode* node, uint32_t depth)
{
    const uint32_t MAX_DEPTH = 5;
    const uint32_t MIN_ENTITIES = 5;

    if (depth >= MAX_DEPTH || node->entityIndices.size() <= MIN_ENTITIES)
        return;

    node->isLeaf = false;
    glm::vec3 center = node->bounds.GetCenter();
    glm::vec3 extent = node->bounds.GetExtent();

    // Create 8 children
    for (int i = 0; i < 8; ++i)
    {
        glm::vec3 newMin, newMax;
        newMin.x = (i & 1) ? center.x : node->bounds.min.x;
        newMin.y = (i & 2) ? center.y : node->bounds.min.y;
        newMin.z = (i & 4) ? center.z : node->bounds.min.z;

        newMax.x = (i & 1) ? node->bounds.max.x : center.x;
        newMax.y = (i & 2) ? node->bounds.max.y : center.y;
        newMax.z = (i & 4) ? node->bounds.max.z : center.z;

        node->children[i] = std::make_unique<OctreeNode>(AABB(newMin, newMax));
    }

    // Distribute entities
    for (uint32_t idx : node->entityIndices)
    {
        const auto& entity = m_Entities[idx];
        glm::mat4 entityTransform = entity.transform.GetTransform();

        // Calculate total entity AABB (approximation)
        AABB entityBounds;
        for (const auto& mesh : entity.mesh.model->GetMeshes())
            entityBounds.Merge(
                mesh.localBounds.Transform(entityTransform * mesh.transform));

        for (int i = 0; i < 8; ++i)
        {
            // Simple check: if center of entity is in child, or if it
            // intersects For simplicity here: use center point
            glm::vec3 eCenter = entityBounds.GetCenter();
            const AABB& cBounds = node->children[i]->bounds;
            if (eCenter.x >= cBounds.min.x && eCenter.x <= cBounds.max.x &&
                eCenter.y >= cBounds.min.y && eCenter.y <= cBounds.max.y &&
                eCenter.z >= cBounds.min.z && eCenter.z <= cBounds.max.z)
            {
                node->children[i]->entityIndices.push_back(idx);
                break;
            }
        }
    }

    node->entityIndices.clear();
    for (int i = 0; i < 8; ++i)
        SubdivideOctree(node->children[i].get(), depth + 1);
}

void Scene::GetVisibleEntities(const Frustum& frustum,
                               std::vector<uint32_t>& outVisibleIndices) const
{
    if (m_OctreeRoot)
        TraverseOctree(m_OctreeRoot.get(), frustum, outVisibleIndices);
    else
    {
        // Fallback to flat list if octree not built
        for (uint32_t i = 0; i < (uint32_t)m_Entities.size(); ++i)
            outVisibleIndices.push_back(i);
    }
}

void Scene::TraverseOctree(const OctreeNode* node, const Frustum& frustum,
                           std::vector<uint32_t>& outVisibleIndices) const
{
    if (!frustum.Intersects(node->bounds)) return;

    if (node->isLeaf)
    {
        for (uint32_t idx : node->entityIndices)
            outVisibleIndices.push_back(idx);
    }
    else
    {
        for (int i = 0; i < 8; ++i)
        {
            if (node->children[i])
                TraverseOctree(node->children[i].get(), frustum,
                               outVisibleIndices);
        }
    }
}

void Scene::LoadSkybox(const std::string& path)
{
    m_SkyboxTexture = ResourceManager::Get().LoadTexture(path, true);
}

void Scene::LoadHDRSkybox(const std::string& path)
{
    m_SkyboxTexture = ResourceManager::Get().LoadHDRTexture(path);
}

void Scene::ClearSkybox()
{
    m_SkyboxTexture = TextureHandle();
}

void Scene::UpdateTLAS()
{
    CH_CORE_INFO("Scene: UpdateTLAS() started.");
    if (!m_Context || !m_Context->IsRayTracingSupported()) return;

    VkDevice device = m_Context->GetDevice();
    std::vector<VkAccelerationStructureInstanceKHR> instances;

    for (const auto& entity : m_Entities)
    {
        auto model = entity.mesh.model;
        if (!model) continue;

        const auto& blasHandles = model->GetBLASHandles();
        const auto& meshes = model->GetMeshes();
        if (blasHandles.empty()) continue;

        glm::mat4 entityTransform = entity.transform.GetTransform();

        for (uint32_t i = 0; i < (uint32_t)meshes.size(); ++i)
        {
            if (i >= (uint32_t)blasHandles.size()) break;

            const Mesh& mesh = meshes[i];
            if (mesh.indexCount == 0) continue;

            VkAccelerationStructureInstanceKHR inst{};

            // Use the same transform logic as GBufferPass
            glm::mat4 modelMatrix = entityTransform * mesh.transform;
            glm::mat4 transpose = glm::transpose(modelMatrix);
            memcpy(&inst.transform, &transpose, sizeof(inst.transform));

            // Match the primitive indexing in Raytrace shaders and G-Buffer
            inst.instanceCustomIndex = entity.primitiveOffset + i;
            inst.mask = 0xFF;

            VkAccelerationStructureDeviceAddressInfoKHR addrInfo{
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
                nullptr, blasHandles[i]};

            inst.accelerationStructureReference =
                vkGetAccelerationStructureDeviceAddressKHR(device, &addrInfo);
            inst.flags =
                VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
            instances.push_back(inst);
        }
    }

    if (instances.empty())
    {
        CH_CORE_INFO(
            "Scene: No instances for TLAS. Destroying old AS if exists.");
        if (m_TopLevelAS != VK_NULL_HANDLE)
        {
            vkDestroyAccelerationStructureKHR(device, m_TopLevelAS, nullptr);
            m_TopLevelAS = VK_NULL_HANDLE;
        }
        m_TLASBuffer.reset();
        m_ASInstanceBuffer.reset();
        return;
    }

    VkDeviceSize instSize =
        instances.size() * sizeof(VkAccelerationStructureInstanceKHR);
    Buffer instStaging(instSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VMA_MEMORY_USAGE_CPU_ONLY);
    instStaging.UploadData(instances.data(), instSize);

    m_ASInstanceBuffer = std::make_unique<Buffer>(
        instSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    {
        ScopedCommandBuffer cmd;
        VkBufferCopy copy{0, 0, instSize};
        vkCmdCopyBuffer(cmd, (VkBuffer)instStaging.GetBuffer(),
                        (VkBuffer)m_ASInstanceBuffer->GetBuffer(), 1, &copy);
    }

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    VkAccelerationStructureGeometryKHR geom{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geom.geometry.instances.sType =
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geom.geometry.instances.data.deviceAddress =
        m_ASInstanceBuffer->GetDeviceAddress();
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geom;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;

    uint32_t count = (uint32_t)instances.size();
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};

    vkGetAccelerationStructureBuildSizesKHR(
        device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo,
        &count, &sizeInfo);

    m_TLASBuffer = std::make_unique<Buffer>(
        sizeInfo.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    if (m_TopLevelAS != VK_NULL_HANDLE)
    {
        vkDestroyAccelerationStructureKHR(device, m_TopLevelAS, nullptr);
        m_TopLevelAS = VK_NULL_HANDLE;
    }

    VkAccelerationStructureCreateInfoKHR createInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    createInfo.buffer = (VkBuffer)m_TLASBuffer->GetBuffer();
    createInfo.size = sizeInfo.accelerationStructureSize;

    vkCreateAccelerationStructureKHR(device, &createInfo, nullptr,
                                     &m_TopLevelAS);

    Buffer scratch(sizeInfo.buildScratchSize,
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                   VMA_MEMORY_USAGE_GPU_ONLY);
    buildInfo.dstAccelerationStructure = m_TopLevelAS;
    buildInfo.scratchData.deviceAddress = scratch.GetDeviceAddress();

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{count, 0, 0, 0};
    const VkAccelerationStructureBuildRangeInfoKHR* pRange = &rangeInfo;
    {
        ScopedCommandBuffer cmd;
        vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRange);
    }
    CH_CORE_INFO("Scene: UpdateTLAS() finished successfully.");
}
} // namespace Chimera
