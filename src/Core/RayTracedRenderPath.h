#pragma once

#include "RenderPath.h"
#include "RayTracingPass.h"
#include "Image.h"
#include "Buffer.h"

namespace Chimera {

    class RayTracedRenderPath : public RenderPath {
    public:
        RayTracedRenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager, VkDescriptorSetLayout globalDescriptorSetLayout);
        ~RayTracedRenderPath();

        virtual void Init() override;
        virtual void OnResize(uint32_t width, uint32_t height) override;
        virtual void OnSceneUpdated() override;
        virtual void OnImGui() override;
        virtual void Render(VkCommandBuffer cmd, uint32_t currentFrame, uint32_t imageIndex, 
                            VkDescriptorSet globalDescriptorSet, const std::vector<VkImage>& swapChainImages,
                            std::function<void(VkCommandBuffer)> uiDrawCallback = nullptr) override;

    private:
        void CreateTopLevelAS();
        void CreateStorageImage();
        void CreateAccumulationImage();
        void CreateRayTracingDescriptorSetLayout();
        void CreateRayTracingDescriptorSets();

        // Helper
        void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels);
        void TransitionImageLayoutImmediate(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels);
        VkCommandBuffer BeginSingleTimeCommands();
        void EndSingleTimeCommands(VkCommandBuffer commandBuffer);
        VkTransformMatrixKHR ToVkMatrix(glm::mat4 model);
        uint64_t GetAccelerationStructureDeviceAddress(VkAccelerationStructureKHR as);

    private:
        std::unique_ptr<RayTracingPass> m_RayTracingPass;

        // Resources
        VkAccelerationStructureKHR m_TopLevelAS = VK_NULL_HANDLE;
        std::unique_ptr<Buffer> m_TLASBuffer;

        std::unique_ptr<Image> m_StorageImage;
        VkFormat m_StorageImageFormat = VK_FORMAT_R8G8B8A8_UNORM;
        
        std::unique_ptr<Image> m_AccumulationImage;

        VkDescriptorSetLayout m_RTDescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_RTDescriptorPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_RTDescriptorSets;
        VkDescriptorSetLayout m_GlobalDescriptorSetLayout;
    };

}
