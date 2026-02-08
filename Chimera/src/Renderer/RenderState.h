#pragma once
#include "pch.h"
#include "Renderer/ChimeraCommon.h"
#include "Scene/SceneCommon.h"
#include "Renderer/Resources/ResourceHandle.h"

namespace Chimera {

    /**
     * @brief 全局帧数据结构，对应 Shader 中的 Global UBO (Set 0)
     */
    struct GlobalFrameData {
        glm::mat4 view;
        glm::mat4 proj;
        glm::mat4 viewInverse;
        glm::mat4 projInverse;
        glm::mat4 viewProjInverse;
        glm::mat4 prevView;
        glm::mat4 prevProj;
        DirectionalLight directionalLight;
        glm::vec2 displaySize;
        glm::vec2 displaySizeInverse;
        uint32_t frameIndex;
        uint32_t frameCount;
        uint32_t displayMode;
        glm::vec4 cameraPos;
    };

    class RenderState {
    public:
        RenderState(const std::shared_ptr<class VulkanContext>& context);
        ~RenderState();

        // 核心 API: 每帧开始时收集数据并上传 GPU
        void Update(uint32_t frameIndex, const GlobalFrameData& data);

        VkDescriptorSet GetDescriptorSet(uint32_t frameIndex) const { return m_DescriptorSets[frameIndex]; }
        VkDescriptorSetLayout GetLayout() const { return m_DescriptorSetLayout; }

    private:
        void CreateDescriptorSetLayout();
        void CreateResources();
        void CreateDescriptorSets();

    private:
        std::shared_ptr<class VulkanContext> m_Context;
        
        VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_DescriptorSets;
        
        struct FrameResources {
            std::unique_ptr<class Buffer> UBO;
        };
        std::vector<FrameResources> m_Frames;
    };

}
