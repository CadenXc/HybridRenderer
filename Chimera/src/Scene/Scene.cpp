#include "pch.h"
#include <volk.h>

// Force complete types for Vulkan handles to satisfy MSVC pedantic checks in this translation unit
struct VkBuffer_T {};
struct VkImage_T {};
struct VkAccelerationStructureKHR_T {};

#include "Scene.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Assets/AssetImporter.h"
#include "Model.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"
#include "Renderer/Backend/RenderContext.h"
#include "Utils/VulkanBarrier.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/Resources/Buffer.h"
#include "Core/Log.h"
#include "Core/Application.h"
#include "Renderer/Backend/Renderer.h"
#include <glm/gtc/matrix_transform.hpp>

namespace Chimera
{
    // Final weapon against opaque type errors: Force cast via uintptr_t
    #define BUF(x) ((VkBuffer)(uintptr_t)(x))

    Scene::Scene(std::shared_ptr<VulkanContext> context)
        : m_Context(context)
    {
        m_Light.direction = glm::vec4(-1.0f, -1.0f, -1.0f, 0.0f);
        m_Light.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        m_Light.intensity = glm::vec4(1.0f);
        CreateDummyResources();
    }

    Scene::~Scene()
    {
        if (m_TopLevelAS != VK_NULL_HANDLE)
        {
            vkDestroyAccelerationStructureKHR(VulkanContext::Get().GetDevice(), m_TopLevelAS, nullptr);
        }
        m_TLASBuffer.reset();
        m_InstanceDataBuffer.reset();
        m_DummyBuffer.reset();
        m_Entities.clear();
    }

    std::shared_ptr<Model> Scene::LoadModel(const std::string& path)
    {
        auto imported = AssetImporter::ImportScene(path, &ResourceManager::Get());
        if (!imported)
        {
            return nullptr;
        }
        uint32_t materialOffset = 0;
        for (size_t i = 0; i < imported->Materials.size(); ++i)
        {
            auto handle = ResourceManager::Get().AddMaterial(std::make_unique<Material>("ModelMat_" + std::to_string(i)));
            auto matObj = ResourceManager::Get().GetMaterial(handle);
            const auto& data = imported->Materials[i];
            matObj->SetAlbedo(data.albedo);
            matObj->SetEmission(data.emission);
            matObj->SetRoughness(data.roughness);
            matObj->SetMetallic(data.metallic);
            matObj->SetTextureIndices(data.albedoTex, data.normalTex, data.metalRoughTex);
            if (i == 0)
            {
                materialOffset = handle.id;
            }
        }
        for (auto& mesh : imported->Meshes)
        {
            mesh.materialIndex += materialOffset;
        }
        auto model = std::make_shared<Model>(m_Context, *imported);
        AddEntity(model, glm::mat4(1.0f), imported->Meshes[0].name);
        ResourceManager::Get().SyncMaterialsToGPU();
        ResourceManager::Get().UpdateSceneDescriptorSet(this);
        return model;
    }

    void Scene::LoadSkybox(const std::string& path)
    {
        CH_CORE_INFO("Scene: Loading Skybox: {0}", path);
        auto handle = ResourceManager::Get().LoadHDRTexture(path);
        m_SkyboxRef = TextureRef(handle);
        ResourceManager::Get().UpdateSceneDescriptorSet(this);
    }

    void Scene::AddEntity(std::shared_ptr<Model> model, const glm::mat4& transform, const std::string& name)
    {
        Entity entity;
        entity.name = name.empty() ? "Unnamed Entity" : name;
        entity.transform.position = glm::vec3(transform[3]);
        entity.transform.rotation = glm::vec3(0.0f);
        entity.transform.scale = glm::vec3(1.0f);
        entity.mesh.model = model;
        m_Entities.push_back(entity);
    }
    
    void Scene::UpdateEntityTRS(uint32_t index, const glm::vec3& t, const glm::vec3& r, const glm::vec3& s)
    {
        if (index < m_Entities.size())
        {
            m_Entities[index].transform.position = t;
            m_Entities[index].transform.rotation = r;
            m_Entities[index].transform.scale = s;
        }
    }

    void Scene::RemoveEntity(uint32_t index)
    {
        if (index < m_Entities.size())
        {
            m_Entities.erase(m_Entities.begin() + index);
        }
    }

    void Scene::RenderMeshes(GraphicsExecutionContext& ctx)
    {
        for (const auto& entity : m_Entities)
        {
            if (!entity.mesh.model)
            {
                continue;
            }
            ForwardPushConstants push;
            glm::mat4 entityTransform = entity.transform.GetTransform();
            auto& meshes = entity.mesh.model->GetMeshes();
            
            // Bypass incomplete type checks using uintptr_t intermediate cast
            VkBuffer vb = BUF(entity.mesh.model->GetVertexBuffer()->GetBuffer());
            VkBuffer ib = BUF(entity.mesh.model->GetIndexBuffer()->GetBuffer());
            VkDeviceSize offsets[] = { 0 };
            
            vkCmdBindVertexBuffers(ctx.GetCommandBuffer(), 0, 1, &vb, offsets);
            vkCmdBindIndexBuffer(ctx.GetCommandBuffer(), ib, 0, VK_INDEX_TYPE_UINT32);

            for (const auto& mesh : meshes)
            {
                push.model = entityTransform * mesh.transform;
                push.normalMatrix = glm::transpose(glm::inverse(push.model));
                push.materialIndex = mesh.materialIndex;
                ctx.PushConstants(push, 0);
                ctx.DrawIndexed(mesh.indexCount, 1, mesh.indexOffset, mesh.vertexOffset, 0);
            }
        }
    }

    void Scene::BuildTLAS()
    {
        if (!VulkanContext::Get().IsRayTracingSupported() || m_Entities.empty())
        {
            return;
        }
        CH_CORE_INFO("Scene: Building TLAS for {0} entities...", m_Entities.size());
        std::vector<VkAccelerationStructureInstanceKHR> instances;
        std::vector<RTInstanceData> instanceData;
        auto vkGetAccelerationStructureDeviceAddressKHR = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(VulkanContext::Get().GetDevice(), "vkGetAccelerationStructureDeviceAddressKHR");
        for (uint32_t i = 0; i < (uint32_t)m_Entities.size(); i++)
        {
            auto& entity = m_Entities[i];
            if (!entity.mesh.model)
            {
                continue;
            }
            auto& meshes = entity.mesh.model->GetMeshes();
            auto& blasHandles = entity.mesh.model->GetBLASHandles();
            glm::mat4 transform = entity.transform.GetTransform();
            for (size_t j = 0; j < meshes.size(); j++)
            {
                VkAccelerationStructureInstanceKHR inst{};
                glm::mat4 transpose = glm::transpose(transform * meshes[j].transform);
                memcpy(&inst.transform, &transpose, sizeof(inst.transform));
                inst.instanceCustomIndex = (uint32_t)instanceData.size();
                inst.mask = 0xFF;
                inst.instanceShaderBindingTableRecordOffset = 0;
                inst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
                VkAccelerationStructureDeviceAddressInfoKHR addressInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
                addressInfo.accelerationStructure = (VkAccelerationStructureKHR)(void*)(uintptr_t)blasHandles[j];
                inst.accelerationStructureReference = vkGetAccelerationStructureDeviceAddressKHR(VulkanContext::Get().GetDevice(), &addressInfo);
                instances.push_back(inst);
                RTInstanceData data{};
                data.vertexAddress = entity.mesh.model->GetVertexBuffer()->GetDeviceAddress();
                data.indexAddress = entity.mesh.model->GetIndexBuffer()->GetDeviceAddress();
                data.materialIndex = meshes[j].materialIndex;
                instanceData.push_back(data);
            }
        }
        if (instances.empty())
        {
            return;
        }
        VkDeviceSize instanceBufferSize = instanceData.size() * sizeof(RTInstanceData);
        if (!m_InstanceDataBuffer || m_InstanceDataBuffer->GetSize() < instanceBufferSize)
        {
            m_InstanceDataBuffer = std::make_unique<Buffer>(instanceBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
            m_InstanceDataBuffer->SetDebugName("RT_InstanceDataBuffer");
        }
        m_InstanceDataBuffer->Update(instanceData.data(), instanceBufferSize);
        VkDeviceSize asInstanceBufferSize = instances.size() * sizeof(VkAccelerationStructureInstanceKHR);
        Buffer instanceStaging(asInstanceBufferSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        instanceStaging.Update(instances.data(), asInstanceBufferSize);
        VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
        geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        geometry.geometry.instances.arrayOfPointers = VK_FALSE;
        geometry.geometry.instances.data.deviceAddress = instanceStaging.GetDeviceAddress();
        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geometry;
        uint32_t count = (uint32_t)instances.size();
        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        vkGetAccelerationStructureBuildSizesKHR(VulkanContext::Get().GetDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &count, &sizeInfo);
        if (!m_TLASBuffer || m_TLASBuffer->GetSize() < sizeInfo.accelerationStructureSize)
        {
            if (m_TopLevelAS != VK_NULL_HANDLE)
            {
                vkDestroyAccelerationStructureKHR(VulkanContext::Get().GetDevice(), m_TopLevelAS, nullptr);
            }
            m_TLASBuffer = std::make_unique<Buffer>(sizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
            VkAccelerationStructureCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
            createInfo.buffer = BUF(m_TLASBuffer->GetBuffer());
            createInfo.size = sizeInfo.accelerationStructureSize;
            createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
            vkCreateAccelerationStructureKHR(VulkanContext::Get().GetDevice(), &createInfo, nullptr, &m_TopLevelAS);
            VulkanContext::Get().SetDebugName((uint64_t)m_TopLevelAS, VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, "RT_Scene_TLAS");
            m_TLASBuffer->SetDebugName("RT_TLAS_BackingBuffer");
        }
        Buffer scratch(sizeInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        buildInfo.dstAccelerationStructure = m_TopLevelAS;
        buildInfo.scratchData.deviceAddress = scratch.GetDeviceAddress();
        VkAccelerationStructureBuildRangeInfoKHR rangeInfo{ count, 0, 0, 0 };
        const VkAccelerationStructureBuildRangeInfoKHR* pRange = &rangeInfo;
        {
            ScopedCommandBuffer cmd;
            vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRange);
        }
        ResourceManager::Get().UpdateSceneDescriptorSet(this);
    }

    void Scene::CreateDummyResources()
    {
        m_DummyBuffer = std::make_unique<Buffer>(1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    }

    void Scene::CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {}
    void Scene::UpdateEntityTransform(uint32_t index, const glm::mat4& transform) {}
}