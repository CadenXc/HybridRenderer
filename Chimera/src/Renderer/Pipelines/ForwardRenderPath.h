#pragma once

#include "Renderer/Pipelines/RenderPath.h"
#include "Renderer/Graph/RenderGraph.h"
#include <memory>

namespace Chimera {

    class ForwardRenderPath : public RenderPath 
	{
    public:
        ForwardRenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager, PipelineManager& pipelineManager, VkDescriptorSetLayout globalDescriptorSetLayout);
        ~ForwardRenderPath();

        virtual void SetupGraph(RenderGraph& graph) override;
        virtual RenderPathType GetType() const override { return RenderPathType::Forward; }
        virtual void OnImGui() override;

    private:
        VkDescriptorSetLayout m_GlobalDescriptorSetLayout;
    };
}
