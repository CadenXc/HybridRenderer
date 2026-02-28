#pragma once

#include "RenderPath.h"

namespace Chimera
{
    class RayQueryRenderPath : public RenderPath
    {
    public:
        RayQueryRenderPath(VulkanContext& context);
        virtual ~RayQueryRenderPath() = default;

        virtual RenderPathType GetType() const override
        {
            return RenderPathType::RayQuery;
        }

    protected:
        virtual void BuildGraph(RenderGraph& graph, std::shared_ptr<Scene> scene) override;
    };
}
