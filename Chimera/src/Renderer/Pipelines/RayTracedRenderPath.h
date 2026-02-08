#pragma once
#include "RenderPath.h"
#include "Renderer/Passes/RaytracePass.h"

namespace Chimera {

    class RayTracedRenderPath : public RenderPath {
    public:
        RayTracedRenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager, PipelineManager& pipelineManager);
        virtual ~RayTracedRenderPath() = default;

        virtual void Render(const RenderFrameInfo& frameInfo) override;
        
        virtual RenderPathType GetType() const override { return RenderPathType::RayTracing; }

    private:
        std::unique_ptr<RaytracePass> m_RaytracePass;
    };

}
