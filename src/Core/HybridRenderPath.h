#pragma once

#include "RenderPath.h"
#include "Image.h"

namespace Chimera {

    enum ShadowMode {
        SHADOW_MODE_RAYTRACED = 0,
        SHADOW_MODE_RASTERIZED = 1,
        SHADOW_MODE_OFF = 2
    };

    enum AmbientOcclusionMode {
        AMBIENT_OCCLUSION_MODE_RAYTRACED = 0,
        AMBIENT_OCCLUSION_MODE_SSAO = 1,
        AMBIENT_OCCLUSION_MODE_OFF = 2
    };

    enum ReflectionMode {
        REFLECTION_MODE_RAYTRACED = 0,
        REFLECTION_MODE_SSR = 1,
        REFLECTION_MODE_OFF = 2
    };

    struct SSAOSettings {
        float radius = 0.75f;
    };

    struct SSRSettings {
        float ray_distance = 25.0f;
        float step_size = 0.1f;
        float thickness = 0.5f;
        int bsearch_steps = 10;
    };

    class HybridRenderPath : public RenderPath {
    public:
        HybridRenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager, VkDescriptorSetLayout globalDescriptorSetLayout);
        ~HybridRenderPath();

        virtual void Init() override;
        virtual void OnResize(uint32_t width, uint32_t height) override;
        virtual void OnSceneUpdated() override;
        virtual void OnImGui() override;
        virtual void Render(VkCommandBuffer cmd, uint32_t currentFrame, uint32_t imageIndex, 
                            VkDescriptorSet globalDescriptorSet, const std::vector<VkImage>& swapChainImages,
                            std::function<void(VkCommandBuffer)> uiDrawCallback = nullptr) override;

    private:
        // Helper
        void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels = 1);
        VkCommandBuffer BeginSingleTimeCommands();
        void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

    private:
        VkDescriptorSetLayout m_GlobalDescriptorSetLayout;
        
        // Settings
        int m_ShadowMode = SHADOW_MODE_OFF; // Default to OFF for now
        int m_AmbientOcclusionMode = AMBIENT_OCCLUSION_MODE_OFF;
        int m_ReflectionMode = REFLECTION_MODE_OFF;
        bool m_DenoiseShadowAndAO = false;

        SSAOSettings m_SSAOSettings;
        SSRSettings m_SSRSettings;
    };

}
