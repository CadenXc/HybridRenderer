#pragma once
#include "RenderPath.h"
#include "Renderer/Passes/GBufferPass.h"
#include "Renderer/Passes/RTShadowAOPass.h"
#include "Renderer/Passes/DeferredLightingPass.h"
#include "Renderer/Passes/SVGFPass.h"
#include "Renderer/Passes/SVGFAtrousPass.h"

namespace Chimera {

    class HybridRenderPath : public RenderPath {
    public:
        HybridRenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager, PipelineManager& pipelineManager);
        virtual ~HybridRenderPath() = default;

        virtual RenderPathType GetType() const override { return RenderPathType::Hybrid; }

        virtual void Render(const RenderFrameInfo& frameInfo) override;

    private:
        std::unique_ptr<GBufferPass> m_GBufferPass;
        std::unique_ptr<RTShadowAOPass> m_RTShadowPass;
        std::unique_ptr<DeferredLightingPass> m_DeferredPass;
        std::unique_ptr<SVGFPass> m_SVGFPass;
        std::unique_ptr<SVGFAtrousPass> m_SVGFAtrousPass;
    };

}
