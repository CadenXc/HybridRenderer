#include "pch.h"
#include "Scene/Scene.h"
#include "Assets/AssetImporter.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Resources/Buffer.h"
#include "Core/Log.h"

namespace Chimera
{
    Scene::Scene(std::shared_ptr<VulkanContext> context, ResourceManager* resourceManager)
        : m_Context(context), m_ResourceManager(resourceManager)
    {
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
        if (m_TopLevelAS != VK_NULL_HANDLE) 
            vkDestroyAccelerationStructureKHR(device, m_TopLevelAS, nullptr);
    }

    std::shared_ptr<Model> Scene::LoadModel(const std::string& path)
    {
        CH_CORE_INFO("Loading model via AssetImporter: {0}", path);
        
        auto importedScene = AssetImporter::ImportScene(path, m_ResourceManager);
        if (!importedScene) return nullptr;

        // Map local material indices to global ones
        uint32_t materialOffset = (uint32_t)m_Materials.size();
        for (auto& mesh : importedScene->Meshes)
        {
            mesh.materialIndex += materialOffset;
        }

        for (const auto& mat : importedScene->Materials)
        {
            m_Materials.push_back(mat);
        }

        UpdateMaterialBuffer();

        auto model = std::make_shared<Model>(m_Context, *importedScene);
        m_Models.push_back(model);

        // Add a default instance for convenience
        AddInstance(model, glm::mat4(1.0f), path);
        m_Instances.back().materialOffset = materialOffset;

        return model;
    }

    void Scene::AddInstance(std::shared_ptr<Model> model, const glm::mat4& transform, const std::string& name)
    {
        Instance inst;
        inst.model = model;
        inst.transform = transform;
        inst.name = name;
        
        // Extract initial position/rotation/scale if needed, 
        // but for a new instance with a provided mat4, we'll just use it.
        // For simplicity, let's just use the matrix provided.
        // We'll set position from the matrix translation part.
        inst.position = glm::vec3(transform[3]);
        inst.rotation = glm::vec3(0.0f);
        inst.scale = glm::vec3(1.0f);

        m_Instances.push_back(inst);
        BuildTLAS();
    }

    void Scene::UpdateInstanceTransform(uint32_t index, const glm::mat4& transform)
    {
        if (index < m_Instances.size())
        {
            m_Instances[index].transform = transform;
            BuildTLAS();
        }
    }

    void Scene::UpdateInstanceTRS(uint32_t index, const glm::vec3& translation, const glm::vec3& rotation, const glm::vec3& scale)
    {
        if (index < m_Instances.size())
        {
            auto& inst = m_Instances[index];
            inst.position = translation;
            inst.rotation = rotation;
            inst.scale = scale;

            glm::mat4 trs = glm::translate(glm::mat4(1.0f), translation);
            trs = glm::rotate(trs, glm::radians(rotation.x), { 1, 0, 0 });
            trs = glm::rotate(trs, glm::radians(rotation.y), { 0, 1, 0 });
            trs = glm::rotate(trs, glm::radians(rotation.z), { 0, 0, 1 });
            trs = glm::scale(trs, scale);
            
            inst.transform = trs;
            BuildTLAS();
        }
    }

    void Scene::RemoveInstance(uint32_t index)
    {
        if (index < m_Instances.size())
        {
            m_Instances.erase(m_Instances.begin() + index);
            if (!m_Instances.empty())
                BuildTLAS();
            else
            {
                // If last instance removed, clean up TLAS
                auto device = m_Context->GetDevice();
                if (m_TopLevelAS != VK_NULL_HANDLE) {
                    VkAccelerationStructureKHR oldAS = m_TopLevelAS;
                    ResourceManager::SubmitResourceFree([device, oldAS]() {
                        vkDestroyAccelerationStructureKHR(device, oldAS, nullptr);
                    });
                    m_TopLevelAS = VK_NULL_HANDLE;
                }
            }
        }
    }

    void Scene::BuildTLAS()
    {
        if (m_Instances.empty()) return;

        auto device = m_Context->GetDevice();

        // 1. Defer cleanup of old resources instead of immediate destruction
        if (m_TopLevelAS != VK_NULL_HANDLE) {
            VkAccelerationStructureKHR oldAS = m_TopLevelAS;
            ResourceManager::SubmitResourceFree([device, oldAS]() {
                vkDestroyAccelerationStructureKHR(device, oldAS, nullptr);
            });
            m_TopLevelAS = VK_NULL_HANDLE;
        }
        
        if (m_TLASBuffer) {
            Buffer* rawPtr = m_TLASBuffer.release();
            ResourceManager::SubmitResourceFree([rawPtr]() {
                delete rawPtr;
            });
        }

        if (m_InstanceDataBuffer) {
            Buffer* rawPtr = m_InstanceDataBuffer.release();
            ResourceManager::SubmitResourceFree([rawPtr]() {
                delete rawPtr;
            });
        }

        std::vector<VkAccelerationStructureInstanceKHR> vkInstances;
        std::vector<InstanceData> instanceDatas;

        for (size_t i = 0; i < m_Instances.size(); ++i)
        {
            const auto& instance = m_Instances[i];
            const auto& blasHandles = instance.model->GetBLASHandles();
            const auto& meshes = instance.model->GetMeshes();

            for (size_t j = 0; j < blasHandles.size(); ++j)
            {
                VkAccelerationStructureDeviceAddressInfoKHR addressInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
                addressInfo.accelerationStructure = blasHandles[j];
                VkDeviceAddress blasAddress = vkGetAccelerationStructureDeviceAddressKHR(m_Context->GetDevice(), &addressInfo);

                uint32_t tlasIndex = (uint32_t)vkInstances.size();

                VkAccelerationStructureInstanceKHR vkInst{};
                glm::mat4 transposed = glm::transpose(instance.transform);
                memcpy(&vkInst.transform, &transposed, sizeof(vkInst.transform));

                vkInst.instanceCustomIndex = tlasIndex; 
                vkInst.mask = 0xFF;
                vkInst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
                vkInst.accelerationStructureReference = blasAddress;
                vkInstances.push_back(vkInst);

                // Build corresponding InstanceData
                InstanceData data{};
                data.vertexAddress = instance.model->GetVertexBufferAddress();
                data.indexAddress = instance.model->GetIndexBufferAddress();
                data.materialIndex = meshes[j].materialIndex; // Absolute material index
                instanceDatas.push_back(data);
            }
        }

        // 1. Build InstanceDataBuffer
        VkDeviceSize instanceDataBufferSize = sizeof(InstanceData) * instanceDatas.size();
        m_InstanceDataBuffer = std::make_unique<Buffer>(
            m_Context->GetAllocator(),
            instanceDataBufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY
        );
        Buffer stagingID(m_Context->GetAllocator(), instanceDataBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
        stagingID.UploadData(instanceDatas.data(), instanceDataBufferSize);
        CopyBuffer(stagingID.GetBuffer(), m_InstanceDataBuffer->GetBuffer(), instanceDataBufferSize);

        // 2. Build TLAS
        VkDeviceSize instanceBufferSize = sizeof(VkAccelerationStructureInstanceKHR) * vkInstances.size();
        Buffer instanceBuffer(m_Context->GetAllocator(), instanceBufferSize, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        
        Buffer stagingBuffer(m_Context->GetAllocator(), instanceBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
        stagingBuffer.UploadData(vkInstances.data(), instanceBufferSize);
        CopyBuffer(stagingBuffer.GetBuffer(), instanceBuffer.GetBuffer(), instanceBufferSize);

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
        uint32_t primitiveCount = (uint32_t)vkInstances.size();
        vkGetAccelerationStructureBuildSizesKHR(m_Context->GetDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primitiveCount, &sizeInfo);

        m_TLASBuffer = std::make_unique<Buffer>(m_Context->GetAllocator(), sizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

        VkAccelerationStructureCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
        createInfo.buffer = m_TLASBuffer->GetBuffer();
        createInfo.size = sizeInfo.accelerationStructureSize;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        vkCreateAccelerationStructureKHR(m_Context->GetDevice(), &createInfo, nullptr, &m_TopLevelAS);

        Buffer scratchBuffer(m_Context->GetAllocator(), sizeInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        buildInfo.dstAccelerationStructure = m_TopLevelAS;
        buildInfo.scratchData.deviceAddress = scratchBuffer.GetDeviceAddress();

        VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{ primitiveCount, 0, 0, 0 };
        VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &buildRangeInfo;

        VkCommandBuffer cmd = m_Context->BeginSingleTimeCommands();
        vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRangeInfo);
        m_Context->EndSingleTimeCommands(cmd);
    }

    void Scene::UpdateMaterialBuffer()
    {
        if (m_Materials.empty()) return;

        VkDeviceSize bufferSize = sizeof(Material) * m_Materials.size();
        
        if (m_MaterialBuffer) {
            Buffer* rawPtr = m_MaterialBuffer.release();
            ResourceManager::SubmitResourceFree([rawPtr]() {
                delete rawPtr;
            });
        }

        m_MaterialBuffer = std::make_unique<Buffer>(
            m_Context->GetAllocator(),
            bufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY
        );

        Buffer stagingBuffer(m_Context->GetAllocator(), bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
        stagingBuffer.UploadData(m_Materials.data(), bufferSize);

        CopyBuffer(stagingBuffer.GetBuffer(), m_MaterialBuffer->GetBuffer(), bufferSize);
    }

    void Scene::CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
    {
        VkCommandBuffer commandBuffer = m_Context->BeginSingleTimeCommands();
        VkBufferCopy copyRegion{ 0, 0, size };
        vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
        m_Context->EndSingleTimeCommands(commandBuffer);
    }
}
