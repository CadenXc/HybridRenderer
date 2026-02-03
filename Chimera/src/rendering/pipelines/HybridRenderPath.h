#pragma once

#include "rendering/core/RenderPath.h"
#include "gfx/resources/Image.h"
#include "rendering/graph/RenderGraph.h"

namespace Chimera {

    class HybridRenderPath : public RenderPath
    {
    public:
        HybridRenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager, VkDescriptorSetLayout globalDescriptorSetLayout);
        ~HybridRenderPath();

        virtual void Init() override;
        virtual void OnImGui() override;
        		virtual void Render(VkCommandBuffer cmd, uint32_t currentFrame, uint32_t imageIndex, 
        							VkDescriptorSet globalDescriptorSet, const std::vector<VkImage>& swapChainImages,
        							std::function<void(VkCommandBuffer)> uiDrawCallback = nullptr) override;
        
        	private:
        		void CreateGBufferResources();
        				void CreateGBufferPipeline();
        				void SetupRenderGraph(uint32_t width, uint32_t height);
        				virtual void OnRecreateResources(uint32_t width, uint32_t height) override;
        		
        				VkFormat FindDepthFormat();
    private:
        VkDescriptorSetLayout m_GlobalDescriptorSetLayout;
        VkPipelineLayout m_GBufferPipelineLayout = VK_NULL_HANDLE;
        VkPipeline m_GBufferPipeline = VK_NULL_HANDLE;

        std::unique_ptr<RenderGraph> m_RenderGraph;

        // G-Buffer Textures
        std::unique_ptr<Image> m_AlbedoImage;   // RT0
        std::unique_ptr<Image> m_NormalImage;   // RT1
        std::unique_ptr<Image> m_MaterialImage; // RT2
        std::unique_ptr<Image> m_MotionImage;   // RT3
        std::unique_ptr<Image> m_DepthImage;    // Depth
    };
}