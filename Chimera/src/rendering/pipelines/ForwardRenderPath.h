#pragma once

#include "rendering/core/RenderPath.h"
#include "gfx/resources/Image.h"
#include <vector>
#include <memory>

namespace Chimera {

    class ForwardRenderPath : public RenderPath 
	{
    public:
        ForwardRenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager, VkDescriptorSetLayout globalDescriptorSetLayout);
        ~ForwardRenderPath();

        virtual void Init() override;
        virtual void OnImGui() override;
        virtual void Render(VkCommandBuffer cmd, uint32_t currentFrame, uint32_t imageIndex, 
                            VkDescriptorSet globalDescriptorSet, const std::vector<VkImage>& swapChainImages,
                            std::function<void(VkCommandBuffer)> uiDrawCallback = nullptr) override;

    private:
        virtual void OnRecreateResources(uint32_t width, uint32_t height) override;
        void CreateRenderPass();
        void CreateFramebuffers();
        void CreateGraphicsPipeline();
        void CreateDepthResources();
        void CreateColorResources(); 

        VkFormat FindDepthFormat();

    private:
        VkDescriptorSetLayout m_GlobalDescriptorSetLayout;
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        VkPipeline m_GraphicsPipeline = VK_NULL_HANDLE;
        
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> m_Framebuffers;

        std::unique_ptr<Image> m_DepthImage;
        std::unique_ptr<Image> m_ColorImage;
    };
}