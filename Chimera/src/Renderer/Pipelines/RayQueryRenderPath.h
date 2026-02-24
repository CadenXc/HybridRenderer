#pragma once
#include "RenderPath.h"

namespace Chimera
{
    class RayQueryRenderPath : public RenderPath
    {
    public:
        RayQueryRenderPath(VulkanContext& context);
        virtual ~RayQueryRenderPath();

        virtual VkSemaphore Render(const RenderFrameInfo& frameInfo) override;
        virtual RenderPathType GetType() const override { return RenderPathType::RayQuery; }
    };
}
