#pragma once

#include "RenderPath.h"

namespace Chimera
{
    class ForwardRenderPath : public RenderPath
    {
    public:
        ForwardRenderPath(VulkanContext& context);
        virtual ~ForwardRenderPath() = default;

        virtual RenderPathType GetType() const override
        {
            return RenderPathType::Forward;
        }

    protected:
        virtual void BuildGraph(RenderGraph& graph, std::shared_ptr<Scene> scene) override;
    };
}
