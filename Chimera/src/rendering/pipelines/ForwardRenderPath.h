#pragma once

#include "rendering/core/RenderPath.h"
#include "gfx/resources/Image.h"

namespace Chimera {

    class ForwardRenderPath : public RenderPath {
    public:
        ForwardRenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager, VkDescriptorSetLayout globalDescriptorSetLayout);
        ~ForwardRenderPath();

        virtual void Init() override;
        virtual void OnImGui() override;
        virtual void Render(VkCommandBuffer cmd, uint32_t currentFrame, uint32_t imageIndex, 
                            VkDescriptorSet globalDescriptorSet, const std::vector<VkImage>& swapChainImages,
                            std::function<void(VkCommandBuffer)> uiDrawCallback = nullptr) override;

    private:
        void Resize(uint32_t width, uint32_t height);
        void CreateGraphicsPipeline();
        void CreateDepthResources();
        void CreateColorResources(); 

        VkFormat FindDepthFormat();
        
        void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels = 1);
        VkCommandBuffer BeginSingleTimeCommands();
        void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

    private:
        VkDescriptorSetLayout m_GlobalDescriptorSetLayout;
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        VkPipeline m_GraphicsPipeline = VK_NULL_HANDLE;

        std::unique_ptr<Image> m_DepthImage;
        std::unique_ptr<Image> m_ColorImage; 
    };

}


