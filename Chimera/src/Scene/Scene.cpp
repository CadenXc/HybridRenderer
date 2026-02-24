#include "pch.h"
#include "Scene.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/Backend/RenderContext.h"
#include "Assets/AssetImporter.h"
#include "Model.h"
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>

namespace Chimera
{

    Scene::Scene(std::shared_ptr<VulkanContext> context)
        : m_Context(context.get())
    {
    }

    Scene::~Scene()
    {
        auto device = m_Context->GetDevice();

        if (m_TopLevelAS != VK_NULL_HANDLE)
        {
            vkDestroyAccelerationStructureKHR(device, m_TopLevelAS, nullptr);
            m_TopLevelAS = VK_NULL_HANDLE;
        }

        m_TLASBuffer.reset();
        m_ASInstanceBuffer.reset();
    }

    void Scene::LoadModel(const std::string& path)
    {
        VkDevice device = m_Context->GetDevice();
        vkDeviceWaitIdle(device);
        m_Entities.clear();

        auto imported = AssetImporter::ImportScene(path, &ResourceManager::Get());
        if (!imported) return;

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
            if (mesh.materialIndex < globalMatIndices.size())
                mesh.materialIndex = globalMatIndices[mesh.materialIndex];
        }

        ResourceManager::Get().SyncMaterialsToGPU();
        auto model = std::make_shared<Model>(m_Context->GetShared(), *imported);

        Entity entity{};
        entity.name = std::filesystem::path(path).filename().string();
        entity.mesh.model = model;
        entity.transform.position = { 0, 0, 0 };
        entity.prevTransform = entity.transform.GetTransform();

        m_Entities.push_back(entity);
        UpdateTLAS();
    }

    void Scene::UpdateEntityTRS(uint32_t index, const glm::vec3& pos, const glm::vec3& rot, const glm::vec3& scale)
    {
        if (index < m_Entities.size())
        {
            auto& e = m_Entities[index];
            e.prevTransform = e.transform.GetTransform();
            e.transform.position = pos;
            e.transform.rotation = rot;
            e.transform.scale = scale;
            UpdateTLAS();
        }
    }

    void Scene::ClearScene()
    {
        m_Entities.clear();
        UpdateTLAS();
    }

    void Scene::LoadSkybox(const std::string& path)
    {
        m_SkyboxTexture = ResourceManager::Get().LoadHDRTexture(path);
    }

    void Scene::ClearSkybox()
    {
        m_SkyboxTexture = TextureHandle();
    }

    void Scene::UpdateTLAS()
    {
        CH_CORE_INFO("Scene: Updating TLAS using volk dispatch...");
        VkDevice device = m_Context->GetDevice();
        
        std::vector<VkAccelerationStructureInstanceKHR> instances;
        uint32_t globalMeshIndex = 0;

        for (auto& entity : m_Entities)
        {
            if (!entity.mesh.model) continue;

            const auto& blasHandles = entity.mesh.model->GetBLASHandles();
            const auto& meshes = entity.mesh.model->GetMeshes();
            glm::mat4 trs = entity.transform.GetTransform();

            uint32_t handleIdx = 0;
            for (uint32_t i = 0; i < (uint32_t)meshes.size(); ++i)
            {
                if (meshes[i].indexCount == 0) continue;
                if (handleIdx >= blasHandles.size()) break;

                VkAccelerationStructureInstanceKHR inst{};
                glm::mat4 transpose = glm::transpose(trs * meshes[i].transform);
                memcpy(&inst.transform, &transpose, sizeof(inst.transform));
                
                inst.instanceCustomIndex = globalMeshIndex++; 
                inst.mask = 0xFF;
                
                VkAccelerationStructureDeviceAddressInfoKHR addrInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR, nullptr, blasHandles[handleIdx++] };
                inst.accelerationStructureReference = vkGetAccelerationStructureDeviceAddressKHR(device, &addrInfo);
                inst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
                instances.push_back(inst);
            }
        }

        if (instances.empty())
        {
            if (m_TopLevelAS != VK_NULL_HANDLE)
            {
                vkDestroyAccelerationStructureKHR(device, m_TopLevelAS, nullptr);
                m_TopLevelAS = VK_NULL_HANDLE;
            }
            return;
        }

        VkDeviceSize instSize = instances.size() * sizeof(VkAccelerationStructureInstanceKHR);
        Buffer instStaging(instSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
        instStaging.UploadData(instances.data(), instSize);
        
        m_ASInstanceBuffer = std::make_unique<Buffer>(instSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        
        { ScopedCommandBuffer cmd; VkBufferCopy copy{ 0, 0, instSize }; vkCmdCopyBuffer(cmd, (VkBuffer)instStaging.GetBuffer(), (VkBuffer)m_ASInstanceBuffer->GetBuffer(), 1, &copy); }

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
        VkAccelerationStructureGeometryKHR geom{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
        geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geom.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        geom.geometry.instances.data.deviceAddress = m_ASInstanceBuffer->GetDeviceAddress();
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR; 
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR; 
        buildInfo.geometryCount = 1; 
        buildInfo.pGeometries = &geom; 
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;

        uint32_t count = (uint32_t)instances.size();
        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &count, &sizeInfo);
        
        m_TLASBuffer = std::make_unique<Buffer>(sizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        
        if (m_TopLevelAS != VK_NULL_HANDLE)
        {
            vkDestroyAccelerationStructureKHR(device, m_TopLevelAS, nullptr);
            m_TopLevelAS = VK_NULL_HANDLE;
        }

        VkAccelerationStructureCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR; 
        createInfo.buffer = (VkBuffer)m_TLASBuffer->GetBuffer(); 
        createInfo.size = sizeInfo.accelerationStructureSize;
        vkCreateAccelerationStructureKHR(device, &createInfo, nullptr, &m_TopLevelAS);
        
        Buffer scratch(sizeInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        buildInfo.dstAccelerationStructure = m_TopLevelAS; 
        buildInfo.scratchData.deviceAddress = scratch.GetDeviceAddress();
        
        VkAccelerationStructureBuildRangeInfoKHR rangeInfo{ count, 0, 0, 0 };
        const VkAccelerationStructureBuildRangeInfoKHR* pRange = &rangeInfo;
        { ScopedCommandBuffer cmd; vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRange); }
    }
}
