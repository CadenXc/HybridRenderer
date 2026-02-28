#pragma once

#include "RenderPath.h"

namespace Chimera
{
    class RayTracedRenderPath : public RenderPath
    {
    public:
        RayTracedRenderPath(VulkanContext& context);
        virtual ~RayTracedRenderPath() = default;

        virtual RenderPathType GetType() const override
        {
            return RenderPathType::RayTracing;
        }

        virtual void OnImGui() override;

    protected:
        virtual void BuildGraph(RenderGraph& graph, std::shared_ptr<Scene> scene) override;

    private:
        bool m_UseAlphaTest = true;
    };
}
