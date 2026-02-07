#pragma once

#include "Renderer/Pipelines/RenderPath.h"
#include "Renderer/Graph/RenderGraph.h"

namespace Chimera {

    class RayTracedRenderPath : public RenderPath
    {
    public:
        RayTracedRenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager, PipelineManager& pipelineManager, VkDescriptorSetLayout globalDescriptorSetLayout);
        ~RayTracedRenderPath();

        virtual void SetupGraph(RenderGraph& graph) override;
        virtual void Update() override;
        
        virtual RenderPathType GetType() const override { return RenderPathType::RayTracing; }
        virtual uint32_t GetFrameCount() const override { return m_FrameCount; }

        virtual void OnSceneUpdated() override;
        virtual void SetScene(std::shared_ptr<Scene> scene) override;
        virtual void OnImGui() override;

    private:
        VkDescriptorSetLayout m_GlobalDescriptorSetLayout;
        uint32_t m_FrameCount = 0;
    };

}