#include "pch.h"
#include "Scene/Scene.h"
#include "Assets/AssetImporter.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Backend/RenderContext.h"
#include "Renderer/Resources/Buffer.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Scene/Model.h"
#include "Core/Log.h"

namespace Chimera
{
    Scene::Scene(std::shared_ptr<VulkanContext> context, ResourceManager* resourceManager)
        : m_Context(context), m_ResourceManager(resourceManager)
    {
        CreateDummyResources();

        m_Camera.view = glm::lookAt(glm::vec3(0.0f, 2.0f, 5.0f), glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        m_Camera.proj = glm::perspective(glm::radians(45.0f), context->GetSwapChainExtent().width / (float)context->GetSwapChainExtent().height, 0.1f, 1000.0f);
        m_Camera.proj[1][1] *= -1; 
        
        m_Camera.viewInverse = glm::inverse(m_Camera.view);
        m_Camera.projInverse = glm::inverse(m_Camera.proj);

        m_Light.direction = glm::vec4(1.0f, -3.0f, 1.0f, 0.0f);
        m_Light.color = glm::vec4(1.0f, 1.0f, 1.0f, 5.0f);
    }

    Scene::~Scene()
    {
        auto device = m_Context->GetDevice();
        vkDeviceWaitIdle(device);

        if (m_TopLevelAS != VK_NULL_HANDLE) 
            vkDestroyAccelerationStructureKHR(device, m_TopLevelAS, nullptr);
    }

    void Scene::CreateDummyResources()
    {
        m_DummyBuffer = std::make_unique<Buffer>(
            m_Context->GetAllocator(),
            1024, 
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY
        );

        {
            ScopedCommandBuffer cmd(m_Context);
            vkCmdFillBuffer(cmd, m_DummyBuffer->GetBuffer(), 0, 1024, 0);
        }

        BuildTLAS();
    }

    std::shared_ptr<Model> Scene::LoadModel(const std::string& path)
    {
        CH_CORE_INFO("Scene: Loading model {0}...", path);
        
        auto importedScene = AssetImporter::ImportScene(path, m_ResourceManager);
        if (!importedScene) {
            CH_CORE_ERROR("Scene: Failed to import model {0}", path);
            return nullptr;
        }

        auto model = std::make_shared<Model>(m_Context, *importedScene);
        m_Models.push_back(model);

        CH_CORE_INFO("Scene: Adding entity for model {0}", path);
        AddEntity(model, glm::mat4(1.0f), path);
        
        // Use the first material from the imported scene as the entity's main material reference
        if (!importedScene->Materials.empty()) {
            // Note: In a more complex setup, each mesh would have its own MaterialRef
            // For now, we assume the entity has a primary material reference.
            // We'll create a Material object in ResourceManager for the FIRST material in the imported scene.
            auto matHandle = m_ResourceManager->AddMaterial(std::make_unique<Material>(path + "_Material"));
            auto& matObj = m_ResourceManager->GetMaterials()[matHandle.id];
            
            // Sync PBR data from imported struct to Material object
            const auto& importedMat = importedScene->Materials[0];
            matObj->SetAlbedo(importedMat.albedo);
            matObj->SetEmission(importedMat.emission);
            matObj->SetRoughness(importedMat.roughness);
            matObj->SetMetallic(importedMat.metallic);
            matObj->SetAlbedoTexture(importedMat.albedoTex);
            matObj->SetNormalTexture(importedMat.normalTex);
            matObj->SetMetalRoughTexture(importedMat.metalRoughTex);

            m_Entities.back().mesh.material = MaterialRef(matHandle);
        }

        m_ResourceManager->SyncMaterialsToGPU();

        return model;
    }

    void Scene::LoadSkybox(const std::string& path)
    {
        m_SkyboxRef = m_ResourceManager->LoadHDRTexture(path);
    }

    void Scene::AddEntity(std::shared_ptr<Model> model, const glm::mat4& transform, const std::string& name)
    {
        Entity entity;
        entity.mesh.model = model;
        entity.name = name;
        entity.transform.position = glm::vec3(transform[3]);
        entity.transform.rotation = glm::vec3(0.0f);
        entity.transform.scale = glm::vec3(1.0f);

        m_Entities.push_back(entity);
        BuildTLAS();
    }

    void Scene::UpdateEntityTransform(uint32_t index, const glm::mat4& transform)
    {
        if (index < m_Entities.size()) {
            m_Entities[index].transform.position = glm::vec3(transform[3]);
            BuildTLAS();
        }
    }

    void Scene::UpdateEntityTRS(uint32_t index, const glm::vec3& translation, const glm::vec3& rotation, const glm::vec3& scale)
    {
        if (index < m_Entities.size()) {
            auto& entity = m_Entities[index];
            entity.transform.position = translation;
            entity.transform.rotation = rotation;
            entity.transform.scale = scale;
            BuildTLAS();
        }
    }

    void Scene::RemoveEntity(uint32_t index)
    {
        if (index < m_Entities.size()) {
            m_Entities.erase(m_Entities.begin() + index);
            BuildTLAS();
        }
    }

    void Scene::BuildTLAS()
    {
        if (!m_Context->IsRayTracingSupported()) {
            return;
        }

        auto device = m_Context->GetDevice();

        if (m_TopLevelAS != VK_NULL_HANDLE) {
            VkAccelerationStructureKHR oldAS = m_TopLevelAS;
            ResourceManager::SubmitResourceFree([device, oldAS]() { vkDestroyAccelerationStructureKHR(device, oldAS, nullptr); });
            m_TopLevelAS = VK_NULL_HANDLE;
        }
        if (m_TLASBuffer) {
            Buffer* raw = m_TLASBuffer.release();
            ResourceManager::SubmitResourceFree([raw]() { delete raw; });
        }
        if (m_InstanceDataBuffer) {
            Buffer* raw = m_InstanceDataBuffer.release();
            ResourceManager::SubmitResourceFree([raw]() { delete raw; });
        }

        std::vector<VkAccelerationStructureInstanceKHR> vkInstances;
        std::vector<RTInstanceData> instanceDatas;

        for (size_t i = 0; i < m_Entities.size(); ++i) {
            const auto& entity = m_Entities[i];
            const auto& model = entity.mesh.model;
            const auto& blasHandles = model->GetBLASHandles();
            const auto& meshes = model->GetMeshes();

            for (size_t j = 0; j < blasHandles.size(); ++j) {
                VkAccelerationStructureDeviceAddressInfoKHR addressInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
                addressInfo.accelerationStructure = blasHandles[j];
                VkDeviceAddress blasAddress = vkGetAccelerationStructureDeviceAddressKHR(m_Context->GetDevice(), &addressInfo);

                VkAccelerationStructureInstanceKHR vkInst{};
                glm::mat4 entityWorldTransform = entity.transform.GetTransform();
                glm::mat4 transposed = glm::transpose(entityWorldTransform * meshes[j].transform);
                memcpy(&vkInst.transform, &transposed, sizeof(vkInst.transform));
                vkInst.instanceCustomIndex = (uint32_t)vkInstances.size();
                vkInst.mask = 0xFF;
                vkInst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
                vkInst.accelerationStructureReference = blasAddress;
                vkInstances.push_back(vkInst);

                RTInstanceData data{};
                data.vertexAddress = model->GetVertexBufferAddress() + (meshes[j].vertexOffset * sizeof(Vertex));
                data.indexAddress = model->GetIndexBufferAddress() + (meshes[j].indexOffset * sizeof(uint32_t));
                // Use the entity's material handle ID
                data.materialIndex = (int)entity.mesh.material.Get().id; 
                instanceDatas.push_back(data);
            }
        }

        uint32_t primitiveCount = (uint32_t)vkInstances.size();
        VkDeviceSize idSize = std::max((size_t)16, sizeof(RTInstanceData) * instanceDatas.size());
        m_InstanceDataBuffer = std::make_unique<Buffer>(m_Context->GetAllocator(), idSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        if (!instanceDatas.empty()) {
            Buffer staging(m_Context->GetAllocator(), idSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
            staging.UploadData(instanceDatas.data(), sizeof(RTInstanceData) * instanceDatas.size());
            CopyBuffer(staging.GetBuffer(), m_InstanceDataBuffer->GetBuffer(), idSize);
        }

        VkDeviceSize instBufferSize = std::max((size_t)16, sizeof(VkAccelerationStructureInstanceKHR) * vkInstances.size());
        Buffer instanceBuffer(m_Context->GetAllocator(), instBufferSize, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        if (!vkInstances.empty()) {
            Buffer staging(m_Context->GetAllocator(), instBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
            staging.UploadData(vkInstances.data(), sizeof(VkAccelerationStructureInstanceKHR) * vkInstances.size());
            CopyBuffer(staging.GetBuffer(), instanceBuffer.GetBuffer(), instBufferSize);
        }

        VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
        geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        geometry.geometry.instances.data.deviceAddress = instanceBuffer.GetDeviceAddress();

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geometry;

        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        vkGetAccelerationStructureBuildSizesKHR(m_Context->GetDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primitiveCount, &sizeInfo);

        m_TLASBuffer = std::make_unique<Buffer>(m_Context->GetAllocator(), sizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        VkAccelerationStructureCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
        createInfo.buffer = m_TLASBuffer->GetBuffer(); createInfo.size = sizeInfo.accelerationStructureSize; createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        vkCreateAccelerationStructureKHR(m_Context->GetDevice(), &createInfo, nullptr, &m_TopLevelAS);

        Buffer scratchBuffer(m_Context->GetAllocator(), std::max((VkDeviceSize)1024, sizeInfo.buildScratchSize), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        buildInfo.dstAccelerationStructure = m_TopLevelAS;
        buildInfo.scratchData.deviceAddress = scratchBuffer.GetDeviceAddress();

        VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{ primitiveCount, 0, 0, 0 };
        VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &buildRangeInfo;
        
        {
            ScopedCommandBuffer cmd(m_Context);
            vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRangeInfo);
        }
    }

    void Scene::CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
    {
        ScopedCommandBuffer cmd(m_Context);
        VkBufferCopy copyRegion{ 0, 0, size };
        vkCmdCopyBuffer(cmd, srcBuffer, dstBuffer, 1, &copyRegion);
    }
}
