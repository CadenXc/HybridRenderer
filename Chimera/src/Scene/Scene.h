#pragma once

#include "SceneCommon.h"
#include "Renderer/Resources/ResourceHandle.h"
#include <vector>
#include <string>
#include <memory>

namespace Chimera
{
    class VulkanContext;
    class Model;

    class Scene
    {
    public:
        Scene(std::shared_ptr<VulkanContext> context);
        ~Scene();

        // Scene Management
        void LoadModel(const std::string& path);
        void UpdateEntityTRS(uint32_t index, const glm::vec3& pos, const glm::vec3& rot, const glm::vec3& scale);
        void ClearScene();

        // Skybox
        void LoadSkybox(const std::string& path);
        void ClearSkybox();
        uint32_t GetSkyboxTextureIndex() const { return m_SkyboxTexture.IsValid() ? m_SkyboxTexture.id : 0xFFFFFFFF; }

        // Lights
        struct DirectionalLight {
            glm::vec4 direction = glm::vec4(0, -1, 0, 0);
            glm::vec4 color = glm::vec4(1, 1, 1, 1);
            glm::vec4 intensity = glm::vec4(3.0f, 0.05f, 0, 0); // x: strength, y: radius
        };
        DirectionalLight& GetLight() { return m_MainLight; }

        // Acceleration Structures
        void UpdateTLAS();
        VkAccelerationStructureKHR GetTLAS() const { return m_TopLevelAS; }
        
        const std::vector<Entity>& GetEntities() const { return m_Entities; }

    private:
        VulkanContext* m_Context;
        std::vector<Entity> m_Entities;
        
        DirectionalLight m_MainLight;
        TextureHandle m_SkyboxTexture;

        VkAccelerationStructureKHR m_TopLevelAS = VK_NULL_HANDLE;
        std::unique_ptr<Buffer> m_TLASBuffer;
        std::unique_ptr<Buffer> m_ASInstanceBuffer; // [NEW] Manage instance buffer lifecycle
    };
}
