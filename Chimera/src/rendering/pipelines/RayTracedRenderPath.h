#pragma once

#include "rendering/core/RenderPath.h"
#include "gfx/resources/Image.h"
#include "gfx/resources/Buffer.h"

namespace Chimera {

    struct RayTracingPushConstants
    {
        glm::vec4 clearColor;
        glm::vec3 lightPos;
        float lightIntensity;
        int frameCount;
    };

    class RayTracedRenderPath : public RenderPath
    {
    public:
        RayTracedRenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager, VkDescriptorSetLayout globalDescriptorSetLayout);
        ~RayTracedRenderPath();

        virtual void Init() override;
        virtual void OnSceneUpdated() override;
        virtual void OnImGui() override;
        virtual void Render(VkCommandBuffer cmd, uint32_t currentFrame, uint32_t imageIndex, 
                            VkDescriptorSet globalDescriptorSet, const std::vector<VkImage>& swapChainImages,
                            std::function<void(VkCommandBuffer)> uiDrawCallback = nullptr) override;

    private:
        virtual void OnRecreateResources(uint32_t width, uint32_t height) override;
        void InitPass(VkAccelerationStructureKHR tlas, Image* storageImage, VkDescriptorSetLayout descriptorSetLayout, VkDescriptorSetLayout globalDescriptorSetLayout);
        void CreatePipeline(VkDescriptorSetLayout descriptorSetLayout, VkDescriptorSetLayout globalDescriptorSetLayout);
        void CreateShaderBindingTable();
        void UpdateDescriptorSets();

        // [新增] 声明这两个辅助函数
        VkCommandBuffer BeginSingleTimeCommands();
        void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

        VkTransformMatrixKHR ToVkMatrix(glm::mat4 model);

    private:
        VkDescriptorSetLayout m_GlobalDescriptorSetLayout;
        VkPipeline m_Pipeline = VK_NULL_HANDLE;
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        
        VkDescriptorPool m_RTDescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_RTDescriptorSetLayout = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_RTDescriptorSets;

        std::unique_ptr<Buffer> m_SBTBuffer;
        VkStridedDeviceAddressRegionKHR m_RaygenRegion{};
        VkStridedDeviceAddressRegionKHR m_MissRegion{};
        VkStridedDeviceAddressRegionKHR m_HitRegion{};
        VkStridedDeviceAddressRegionKHR m_CallRegion{};

        VkAccelerationStructureKHR m_PassTLAS = VK_NULL_HANDLE;
        Image* m_PassStorageImage = nullptr;
        
        VkImageLayout m_StorageImageLayout = VK_IMAGE_LAYOUT_GENERAL;
        uint32_t m_FrameCount = 0;

        std::unique_ptr<Image> m_StorageImage;
    };
}