#pragma once

#include "RenderPath.h"

namespace Chimera
{
    class RayTracedRenderPath : public RenderPath
    {
    public:
        RayTracedRenderPath(VulkanContext& context, std::shared_ptr<Scene> scene);
        virtual ~RayTracedRenderPath();

        virtual VkSemaphore Render(const RenderFrameInfo& frameInfo) override;
        virtual RenderPathType GetType() const override { return RenderPathType::RayTracing; }
    };
}