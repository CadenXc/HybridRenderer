#pragma once

#include "Renderer/Pipelines/RenderPath.h"
#include "Renderer/Graph/RenderGraph.h"
#include <memory>

namespace Chimera {

    class ForwardRenderPath : public RenderPath 
	{
    public:
        ForwardRenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager, PipelineManager& pipelineManager);
        ~ForwardRenderPath();

        virtual RenderPathType GetType() const override { return RenderPathType::Forward; }
        
        virtual void Render(const RenderFrameInfo& frameInfo) override;
    };
}
