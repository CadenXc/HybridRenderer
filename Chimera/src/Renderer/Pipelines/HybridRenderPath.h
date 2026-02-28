#pragma once

#include "RenderPath.h"

namespace Chimera
{
    class HybridRenderPath : public RenderPath
    {
    public:
        HybridRenderPath(VulkanContext& context);
        virtual ~HybridRenderPath() = default;

        virtual RenderPathType GetType() const override
        {
            return RenderPathType::Hybrid;
        }

    protected:
        virtual void BuildGraph(RenderGraph& graph, std::shared_ptr<Scene> scene) override;
    };
}
