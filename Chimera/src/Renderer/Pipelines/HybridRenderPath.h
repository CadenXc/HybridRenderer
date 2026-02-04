#pragma once

#include "Renderer/Pipelines/RenderPath.h"
#include "Renderer/Graph/RenderGraph.h"

namespace Chimera {

    class HybridRenderPath : public RenderPath
    {
    public:
        HybridRenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager, PipelineManager& pipelineManager, VkDescriptorSetLayout globalDescriptorSetLayout);
        ~HybridRenderPath();

        virtual void SetupGraph(RenderGraph& graph) override;
        virtual RenderPathType GetType() const override { return RenderPathType::Hybrid; }
        virtual void OnImGui() override;
        
    private:
        VkDescriptorSetLayout m_GlobalDescriptorSetLayout;
    };
}
