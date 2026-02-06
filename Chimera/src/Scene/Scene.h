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
        Scene(std::shared_ptr<VulkanContext> context, ResourceManager* resourceManager);
        ~Scene();

        std::shared_ptr<Model> LoadModel(const std::string& path);
        void AddInstance(std::shared_ptr<Model> model, const glm::mat4& transform = glm::mat4(1.0f), const std::string& name = "Instance");

        const std::vector<Instance>& GetInstances() const { return m_Instances; }
        void UpdateInstanceTransform(uint32_t index, const glm::mat4& transform);
        void UpdateInstanceTRS(uint32_t index, const glm::vec3& translation, const glm::vec3& rotation, const glm::vec3& scale);
        void RemoveInstance(uint32_t index);
        const std::vector<Material>& GetMaterials() const { return m_Materials; }
        VkBuffer GetMaterialBuffer() const { return m_MaterialBuffer ? m_MaterialBuffer->GetBuffer() : VK_NULL_HANDLE; }
        VkBuffer GetInstanceDataBuffer() const { return m_InstanceDataBuffer ? m_InstanceDataBuffer->GetBuffer() : VK_NULL_HANDLE; }
        
        std::shared_ptr<VulkanContext> GetContext() const { return m_Context; }
        VkAccelerationStructureKHR GetTLAS() const { return m_TopLevelAS; }

        Camera& GetCamera() { return m_Camera; }
        DirectionalLight& GetLight() { return m_Light; }

        void BuildTLAS();
        void UpdateMaterialBuffer();

    private:
        void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

    private:
        std::shared_ptr<VulkanContext> m_Context;
        ResourceManager* m_ResourceManager = nullptr;

        std::vector<std::shared_ptr<Model>> m_Models;
        std::vector<Instance> m_Instances;
        std::vector<Material> m_Materials;
        std::unique_ptr<Buffer> m_MaterialBuffer;
        std::unique_ptr<Buffer> m_InstanceDataBuffer;

        VkAccelerationStructureKHR m_TopLevelAS = VK_NULL_HANDLE;
        std::unique_ptr<Buffer> m_TLASBuffer;

        Camera m_Camera;
        DirectionalLight m_Light;
        
        // 我们以后会将纹理管理进一步移交给 ResourceManager，目前保留在 Scene 用于展示
        std::vector<std::unique_ptr<Image>> m_LoadedTextures;
        std::unordered_map<std::string, int> m_TextureMap;
    };
}