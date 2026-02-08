#include "pch.h"
#include "Scene.h"
#include "Assets/AssetImporter.h"
#include "Model.h"
#include "Renderer/Graph/GraphicsExecutionContext.h"
#include "Utils/VulkanBarrier.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/Resources/Buffer.h"
#include "Core/Log.h"
#include <glm/gtc/matrix_transform.hpp>

namespace Chimera {

    Scene::Scene(std::shared_ptr<VulkanContext> context, ResourceManager* resourceManager)
        : m_Context(context), m_ResourceManager(resourceManager)
    {
        m_Light.direction = glm::vec4(-1.0f, -1.0f, -1.0f, 0.0f);
        m_Light.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        m_Light.intensity = glm::vec4(1.0f);
        CreateDummyResources();
    }

    Scene::~Scene()
    {
        VkDevice device = m_Context->GetDevice();
        if (m_TopLevelAS != VK_NULL_HANDLE) {
            vkDestroyAccelerationStructureKHR(device, m_TopLevelAS, nullptr);
        }
    }

    std::shared_ptr<Model> Scene::LoadModel(const std::string& path) {
        auto imported = AssetImporter::ImportScene(path, m_ResourceManager);
        if (!imported) {
            CH_CORE_ERROR("Scene: Failed to load model from {0}", path);
            return nullptr;
        }
        
        // 1. Register materials in ResourceManager
        uint32_t materialOffset = 0;
        for (size_t i = 0; i < imported->Materials.size(); ++i) {
            auto handle = m_ResourceManager->AddMaterial(std::make_unique<Material>("ModelMat_" + std::to_string(i)));
            auto matObj = m_ResourceManager->GetMaterial(handle);
            
            // Setup the material with imported data
            const auto& data = imported->Materials[i];
            matObj->SetAlbedo(data.albedo);
            matObj->SetEmission(data.emission);
            matObj->SetRoughness(data.roughness);
            matObj->SetMetallic(data.metallic);
            matObj->SetTextureIndices(data.albedoTex, data.normalTex, data.metalRoughTex);

            if (i == 0) materialOffset = handle.id;
        }

        // 2. Adjust mesh material indices to point to the global indices in ResourceManager
        for (auto& mesh : imported->Meshes) {
            mesh.materialIndex += materialOffset;
        }

        auto model = std::make_shared<Model>(m_Context, *imported);
        
        // Auto-instantiate the model in the scene
        AddEntity(model, glm::mat4(1.0f), imported->Meshes[0].name);
        
        // 3. Sync everything to GPU
        m_ResourceManager->SyncMaterialsToGPU();
        
        return model;
    }

    void Scene::AddEntity(std::shared_ptr<Model> model, const glm::mat4& transform, const std::string& name)
    {
        Entity entity;
        entity.name = name.empty() ? "Unnamed Entity" : name;
        
        // Extract translation, rotation, scale from transform matrix
        entity.transform.position = glm::vec3(transform[3]);
        // For rotation/scale we'll keep defaults or might need more complex extraction
        entity.transform.rotation = glm::vec3(0.0f);
        entity.transform.scale = glm::vec3(1.0f);
        
        entity.mesh.model = model;
        m_Entities.push_back(entity);
        
        CH_CORE_INFO("Scene: Added entity '{0}'", entity.name);
    }
    
    void Scene::UpdateEntityTRS(uint32_t index, const glm::vec3& t, const glm::vec3& r, const glm::vec3& s) {
        if (index < m_Entities.size()) {
            m_Entities[index].transform.position = t;
            m_Entities[index].transform.rotation = r;
            m_Entities[index].transform.scale = s;
        }
    }

    void Scene::RemoveEntity(uint32_t index) {
        if (index < m_Entities.size()) {
            m_Entities.erase(m_Entities.begin() + index);
        }
    }

    void Scene::RenderMeshes(GraphicsExecutionContext& ctx)
    {
        for (const auto& entity : m_Entities) {
            if (!entity.mesh.model) continue;

            struct MeshPushConstants {
                glm::mat4 model;
                glm::mat4 normalMatrix;
                int materialIndex;
            } push;

            glm::mat4 entityTransform = entity.transform.GetTransform();
            
            auto& meshes = entity.mesh.model->GetMeshes();
            VkBuffer vertexBuffer = entity.mesh.model->GetVertexBuffer();
            VkBuffer indexBuffer = entity.mesh.model->GetIndexBuffer();
            VkDeviceSize offsets[] = { 0 };
            
            vkCmdBindVertexBuffers(ctx.GetCommandBuffer(), 0, 1, &vertexBuffer, offsets);
            vkCmdBindIndexBuffer(ctx.GetCommandBuffer(), indexBuffer, 0, VK_INDEX_TYPE_UINT32);

            for (const auto& mesh : meshes) {
                push.model = entityTransform * mesh.transform;
                push.normalMatrix = glm::transpose(glm::inverse(push.model));
                push.materialIndex = mesh.materialIndex;

                ctx.PushConstants(push, 0);
                ctx.DrawIndexed(mesh.indexCount, 1, mesh.indexOffset, mesh.vertexOffset, 0);
            }
        }
    }

    void Scene::BuildTLAS() {}

    void Scene::CreateDummyResources()
    {
        // [FIX] Buffer constructor usually takes Allocator or Context + usage
        m_DummyBuffer = std::make_unique<Buffer>(m_Context->GetAllocator(), 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    }

    void Scene::CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {}
}