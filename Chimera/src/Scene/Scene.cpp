#include "pch.h"
#include "Scene.h"
#include "Scene/SceneCommon.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Backend/RenderContext.h"
#include "Renderer/Backend/PipelineManager.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"
#include "Renderer/Backend/ShaderCommon.h"
#include "Assets/AssetImporter.h"
#include "Utils/VulkanBarrier.h"
#include "Core/Log.h"

namespace Chimera
{

    Scene::Scene(std::shared_ptr<VulkanContext> context) : m_Context(context)
    {
        m_Light.direction = glm::vec4(1.0f, -1.0f, 1.0f, 0.0f);
        m_Light.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        m_Light.intensity = glm::vec4(1.0f);
        CreateDummyResources();
    }

    Scene::~Scene()
    {
        if (m_Context)
        {
            VkDevice device = m_Context->GetDevice();
            if (m_TopLevelAS != VK_NULL_HANDLE)
            {
                auto vkDestroyAS = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureKHR");
                vkDestroyAS(device, m_TopLevelAS, nullptr);
            }
        }
    }

    std::shared_ptr<Model> Scene::LoadModel(const std::string& path)
    {
        auto imported = AssetImporter::ImportScene(path, &ResourceManager::Get());
        if (!imported)
        {
            return nullptr;
        }

        std::vector<uint32_t> globalIndices;
        for (size_t i = 0; i < imported->Materials.size(); ++i)
        {
            std::string name = path + "_mat_" + std::to_string(i);
            globalIndices.push_back(ResourceManager::Get().AddMaterial(std::make_unique<Material>(name, imported->Materials[i]), name).id);
        }

        for (auto& mesh : imported->Meshes)
        {
            if (mesh.materialIndex >= 0 && mesh.materialIndex < globalIndices.size())
                mesh.materialIndex = globalIndices[mesh.materialIndex];
        }

        ResourceManager::Get().SyncMaterialsToGPU();
        auto model = std::make_shared<Model>(m_Context, *imported);
        m_Models.push_back(model);
        AddEntity(model, glm::mat4(1.0f), "LoadedModel");
        BuildTLAS();
        return model;
    }

        void Scene::LoadSkybox(const std::string& path)

        {

            m_SkyboxRef = ResourceManager::Get().LoadHDRTexture(path);

        }

    void Scene::ClearSkybox()
    {
        m_SkyboxRef = TextureRef();
    }

    void Scene::AddEntity(std::shared_ptr<Model> model, const glm::mat4& transform, const std::string& name)
    {
        Entity e;
        e.name = name;
        e.mesh.model = model;
        e.transform.position = glm::vec3(transform[3]);
        e.prevTransform = transform;
        m_Entities.push_back(e);
    }

    void Scene::UpdateEntityTRS(uint32_t index, const glm::vec3& t, const glm::vec3& r, const glm::vec3& s)
    {
        if (index < m_Entities.size())
        {
            auto& e = m_Entities[index];
            e.transform.position = t; e.transform.rotation = r; e.transform.scale = s;
        }
    }

    void Scene::BuildTLAS()
    {
        VkDevice device = m_Context->GetDevice();
        auto vkGetASDeviceAddress = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(device, "vkGetAccelerationStructureDeviceAddressKHR");
        auto vkGetASBuildSizes = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(device, "vkGetAccelerationStructureBuildSizesKHR");
        auto vkCreateAS = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(device, "vkCreateAccelerationStructureKHR");
        auto vkCmdBuildAS = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(device, "vkCmdBuildAccelerationStructuresKHR");

        std::vector<VkAccelerationStructureInstanceKHR> instances;
        std::vector<RTInstanceData> rtInstanceData;

        for (uint32_t i = 0; i < (uint32_t)m_Entities.size(); ++i)
        {
            auto& entity = m_Entities[i];
            if (!entity.mesh.model)
            {
                continue;
            }
            const auto& blasHandles = entity.mesh.model->GetBLASHandles();
            const auto& meshes = entity.mesh.model->GetMeshes();
            glm::mat4 trs = entity.transform.GetTransform();

            for (uint32_t j = 0; j < (uint32_t)blasHandles.size(); ++j)
            {
                // 1. Vulkan AS Instance
                VkAccelerationStructureInstanceKHR inst{};
                glm::mat4 transpose = glm::transpose(trs * meshes[j].transform);
                memcpy(&inst.transform, &transpose, sizeof(inst.transform));
                
                // instanceCustomIndex points to our rtInstanceData array
                inst.instanceCustomIndex = (uint32_t)rtInstanceData.size();
                inst.mask = 0xFF;
                
                VkAccelerationStructureDeviceAddressInfoKHR addrInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR, nullptr, blasHandles[j] };
                inst.accelerationStructureReference = vkGetASDeviceAddress(device, &addrInfo);
                inst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
                instances.push_back(inst);

                // 2. Custom RT Instance Data for Shader
                RTInstanceData data{};
                data.vertexAddress = entity.mesh.model->GetVertexBuffer()->GetDeviceAddress();
                data.indexAddress  = entity.mesh.model->GetIndexBuffer()->GetDeviceAddress();
                data.materialIndex = meshes[j].materialIndex;
                rtInstanceData.push_back(data);
            }
        }

        if (instances.empty())
        {
            if (m_TopLevelAS != VK_NULL_HANDLE)
            {
                auto vkDestroyAS = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(m_Context->GetDevice(), "vkDestroyAccelerationStructureKHR");
                vkDestroyAS(m_Context->GetDevice(), m_TopLevelAS, nullptr);
                m_TopLevelAS = VK_NULL_HANDLE;
            }
            return;
        }

        // --- 1. Upload Vulkan Instances ---
        VkDeviceSize instSize = instances.size() * sizeof(VkAccelerationStructureInstanceKHR);
        Buffer instStaging(instSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
        instStaging.UploadData(instances.data(), instSize);
        auto asInstanceBuffer = std::make_unique<Buffer>(instSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        { ScopedCommandBuffer cmd; VkBufferCopy copy{ 0, 0, instSize }; vkCmdCopyBuffer(cmd, (VkBuffer)instStaging.GetBuffer(), (VkBuffer)asInstanceBuffer->GetBuffer(), 1, &copy); }

        // --- 2. Upload Custom RTInstanceData ---
        VkDeviceSize rtInstSize = rtInstanceData.size() * sizeof(RTInstanceData);
        Buffer rtStaging(rtInstSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
        rtStaging.UploadData(rtInstanceData.data(), rtInstSize);
        m_InstanceDataBuffer = std::make_unique<Buffer>(rtInstSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        { ScopedCommandBuffer cmd; VkBufferCopy copy{ 0, 0, rtInstSize }; vkCmdCopyBuffer(cmd, (VkBuffer)rtStaging.GetBuffer(), (VkBuffer)m_InstanceDataBuffer->GetBuffer(), 1, &copy); }

        // --- 3. Build TLAS ---
        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
        VkAccelerationStructureGeometryKHR geom{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
        geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geom.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        geom.geometry.instances.data.deviceAddress = asInstanceBuffer->GetDeviceAddress();
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR; 
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR; 
        buildInfo.geometryCount = 1; 
        buildInfo.pGeometries = &geom; 
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;

        uint32_t count = (uint32_t)instances.size();
        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        vkGetASBuildSizes(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &count, &sizeInfo);
        
        m_TLASBuffer = std::make_unique<Buffer>(sizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        
        VkAccelerationStructureCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR; 
        createInfo.buffer = (VkBuffer)m_TLASBuffer->GetBuffer(); 
        createInfo.size = sizeInfo.accelerationStructureSize;
        vkCreateAS(device, &createInfo, nullptr, &m_TopLevelAS);
        
        Buffer scratch(sizeInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        buildInfo.dstAccelerationStructure = m_TopLevelAS; 
        buildInfo.scratchData.deviceAddress = scratch.GetDeviceAddress();
        
        VkAccelerationStructureBuildRangeInfoKHR rangeInfo{ count, 0, 0, 0 };
        const VkAccelerationStructureBuildRangeInfoKHR* pRange = &rangeInfo;
        { ScopedCommandBuffer cmd; vkCmdBuildAS(cmd, 1, &buildInfo, &pRange); }
    }

    void Scene::RenderMeshes(GraphicsExecutionContext& ctx)
    {
        VkCommandBuffer cmd = ctx.GetCommandBuffer();
        for (auto& entity : m_Entities)
        {
            if (!entity.mesh.model)
            {
                continue;
            }
            VkBuffer vBuf = (VkBuffer)entity.mesh.model->GetVertexBuffer()->GetBuffer();
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &vBuf, &offset);
            vkCmdBindIndexBuffer(cmd, (VkBuffer)entity.mesh.model->GetIndexBuffer()->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

            glm::mat4 currentTransform = entity.transform.GetTransform();
            for (const auto& mesh : entity.mesh.model->GetMeshes())
            {
                GBufferPushConstants pc;
                pc.model = currentTransform * mesh.transform;
                pc.normalMatrix = glm::transpose(glm::inverse(pc.model));
                pc.prevModel = entity.prevTransform * mesh.transform;
                pc.materialIndex = mesh.materialIndex;
                ctx.PushConstants(VK_SHADER_STAGE_ALL, pc);
                ctx.DrawIndexed(mesh.indexCount, 1, mesh.indexOffset, mesh.vertexOffset, 0);
            }
            entity.prevTransform = currentTransform; // [FIX] 更新上一帧变换
        }
    }

        void Scene::CreateDummyResources()

        {

            m_DummyBuffer = std::make_unique<Buffer>(1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

        }
}