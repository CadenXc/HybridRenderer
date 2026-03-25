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
        void LoadModel(const std::string &path);
        void UpdateEntityTRS(uint32_t index, const glm::vec3 &pos, const glm::vec3 &rot, const glm::vec3 &scale);
        void OnUpdate(float ts);
        void ClearScene();

        // Skybox
        void LoadSkybox(const std::string &path);
        void LoadHDRSkybox(const std::string &path);
        void ClearSkybox();

        uint32_t GetSkyboxTextureIndex() const
        {
            return m_SkyboxTexture.IsValid() ? m_SkyboxTexture.id : 0xFFFFFFFF;
        }

        // Lights
        void AddLight(const Light &light) { m_Lights.push_back(light); }
        std::vector<Light> &GetLights() { return m_Lights; }
        const std::vector<Light> &GetLights() const { return m_Lights; }

        // [COMPAT] Return first light as main light for old passes
        Light &GetMainLight()
        {
            if (m_Lights.empty())
            {
                Light defaultLight;
                defaultLight.position.w = (float)LightType::Directional;
                defaultLight.direction = glm::vec4(glm::normalize(glm::vec3(0.5f, -0.8f, 0.2f)), 0.0f);
                defaultLight.color = glm::vec4(1.0f, 1.0f, 1.0f, 3.0f); // Intensity in Alpha
                m_Lights.push_back(defaultLight);
            }
            return m_Lights[0];
        }

        // Acceleration Structures
        void UpdateTLAS();

        VkAccelerationStructureKHR GetTLAS() const
        {
            return m_TopLevelAS;
        }

        const std::vector<Entity> &GetEntities() const
        {
            return m_Entities;
        }

        void MarkDirty() { m_NeedsTLASRebuild = true; }
        void MarkMaterialDirty() { m_NeedsMaterialSync = true; }

        // Hierarchy Management
        void UpdateWorldTransforms();
        
        // Octree
        void BuildOctree();
        void GetVisibleEntities(const Frustum& frustum, std::vector<uint32_t>& outVisibleIndices) const;

    private:
        void ComputeWorldTransform(uint32_t nodeIndex, const glm::mat4 &parentTransform);
        void SubdivideOctree(OctreeNode* node, uint32_t depth);
        void TraverseOctree(const OctreeNode* node, const Frustum& frustum, std::vector<uint32_t>& outVisibleIndices) const;

    private:
        VulkanContext *m_Context;
        std::vector<Entity> m_Entities;
        std::vector<Node> m_Nodes;
        std::vector<glm::mat4> m_WorldTransforms; // Cached global matrices

        std::unique_ptr<OctreeNode> m_OctreeRoot;

        std::vector<Light> m_Lights;
        TextureHandle m_SkyboxTexture;

        bool m_NeedsTLASRebuild = false;
        bool m_NeedsMaterialSync = false;

        VkAccelerationStructureKHR m_TopLevelAS = VK_NULL_HANDLE;
        std::unique_ptr<Buffer> m_TLASBuffer;
        std::unique_ptr<Buffer> m_ASInstanceBuffer;
    };
}
