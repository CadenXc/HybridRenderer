#pragma once

#include "Renderer/Pipelines/RenderPath.h"
#include "Renderer/Graph/RenderGraph.h"

namespace Chimera {

    class HybridRenderPath : public RenderPath
    {
    public:
        HybridRenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager, PipelineManager& pipelineManager, VkDescriptorSetLayout globalDescriptorSetLayout);
        ~HybridRenderPath();

                virtual void Init() override;
                virtual void Update() override;
                virtual void SetupGraph(RenderGraph& graph) override;
                virtual void Render(VkCommandBuffer cmd, uint32_t currentFrame, uint32_t imageIndex, 
                                  VkDescriptorSet globalDescriptorSet, const std::vector<VkImage>& swapChainImages,
                                  std::function<void(VkCommandBuffer)> uiDrawCallback) override;

                virtual RenderPathType GetType() const override { return RenderPathType::Hybrid; }
                virtual uint32_t GetFrameCount() const override { return m_FrameCount; }

                virtual void OnImGui() override;

            private:
                void ResizeHistoryImages(uint32_t width, uint32_t height);

            private:
                VkDescriptorSetLayout m_GlobalDescriptorSetLayout;
                uint32_t m_FrameCount = 0;

                // Bloom Settings
                float m_BloomThreshold = 1.0f;
                float m_BloomIntensity = 0.5f;

                // SVGF History Buffers
                std::unique_ptr<Image> m_PrevNormal;
                std::unique_ptr<Image> m_PrevDepth;
                std::unique_ptr<Image> m_ShadowAOHistory;
                std::unique_ptr<Image> m_MomentsHistory;
                std::unique_ptr<Image> m_IntegratedShadowAO[2]; // Ping-pong for filter
            };

        
}
