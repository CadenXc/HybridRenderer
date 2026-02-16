#pragma once

#include "RenderPath.h"

namespace Chimera
{
    class ForwardRenderPath : public RenderPath
    {
    public:
        ForwardRenderPath(VulkanContext& context, std::shared_ptr<Scene> scene);
        virtual ~ForwardRenderPath();

        virtual VkSemaphore Render(const RenderFrameInfo& frameInfo) override;
        virtual RenderPathType GetType() const override { return RenderPathType::Forward; }
    };
}