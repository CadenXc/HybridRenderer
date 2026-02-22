#pragma once

#include "RenderPath.h"

namespace Chimera
{
    class HybridRenderPath : public RenderPath
    {
    public:
        HybridRenderPath(VulkanContext& context);
        virtual ~HybridRenderPath();

        virtual VkSemaphore Render(const RenderFrameInfo& frameInfo) override;
        virtual RenderPathType GetType() const override
        {
            return RenderPathType::Hybrid;
        }
    };
}