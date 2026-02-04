#pragma once

#include "Renderer/Pipelines/RenderPath.h"
#include "Renderer/Graph/RenderGraph.h"

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
        RayTracedRenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager, PipelineManager& pipelineManager, VkDescriptorSetLayout globalDescriptorSetLayout);
        ~RayTracedRenderPath();

        virtual void SetupGraph(RenderGraph& graph) override;
        virtual RenderPathType GetType() const override { return RenderPathType::RayTracing; }
        virtual void OnSceneUpdated() override;
        virtual void SetScene(std::shared_ptr<Scene> scene) override;
        virtual void OnImGui() override;
        void ResetAccumulation() { m_FrameCount = 0; }
        
    private:
        VkDescriptorSetLayout m_GlobalDescriptorSetLayout;
        uint32_t m_FrameCount = 0;
    };
}
