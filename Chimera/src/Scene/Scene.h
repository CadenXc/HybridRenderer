#pragma once

#include "pch.h"
#include "Scene/SceneCommon.h"
#include "Scene/Model.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Resources/Buffer.h"
#include "Renderer/Resources/ResourceManager.h"
#include <unordered_map>
#include <vector>
#include <memory>

namespace Chimera
{
    class Scene
    {
    public:
        Scene(std::shared_ptr<VulkanContext> context);
        ~Scene();

        std::shared_ptr<Model> LoadModel(const std::string& path);
        void LoadSkybox(const std::string& path);
        void ClearSkybox();
        void AddEntity(std::shared_ptr<Model> model, const glm::mat4& transform = glm::mat4(1.0f), const std::string& name = "Entity");

        const std::vector<Entity>& GetEntities() const
        {
            return m_Entities;
        }

        void UpdateEntityTransform(uint32_t index, const glm::mat4& transform);
        void UpdateEntityTRS(uint32_t index, const glm::vec3& translation, const glm::vec3& rotation, const glm::vec3& scale);
        void RemoveEntity(uint32_t index);
        
        int GetSkyboxTextureIndex() const
        {
            return m_SkyboxRef.IsValid() ? (int)m_SkyboxRef.Get().id : -1;
        }

        VkBuffer GetMaterialBuffer() const
        {
            return ResourceManager::Get().GetMaterialBuffer();
        }

        VkBuffer GetInstanceDataBuffer() const
        {
            return (VkBuffer)(m_InstanceDataBuffer ? m_InstanceDataBuffer->GetBuffer() : m_DummyBuffer->GetBuffer());
        }
        
        std::shared_ptr<VulkanContext> GetContext() const
        {
            return m_Context;
        }

        VkAccelerationStructureKHR GetTLAS() const
        {
            return m_TopLevelAS;
        }

        Camera& GetCamera()
        {
            return m_Camera;
        }

        const Camera& GetCamera() const
        {
            return m_Camera;
        }

        DirectionalLight& GetLight()
        {
            return m_Light;
        }

        const DirectionalLight& GetLight() const
        {
            return m_Light;
        }

        void BuildTLAS();
        void RenderMeshes(class GraphicsExecutionContext& ctx);

    private:
        void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
        void CreateDummyResources();

    private:
        std::shared_ptr<VulkanContext> m_Context;

        std::vector<std::shared_ptr<Model>> m_Models;
        std::vector<Entity> m_Entities;
        std::unique_ptr<Buffer> m_InstanceDataBuffer;
        std::unique_ptr<Buffer> m_DummyBuffer;

        VkAccelerationStructureKHR m_TopLevelAS = VK_NULL_HANDLE;
        std::unique_ptr<Buffer> m_TLASBuffer;

        Camera m_Camera;
        DirectionalLight m_Light;
        TextureRef m_SkyboxRef;
    };
}
