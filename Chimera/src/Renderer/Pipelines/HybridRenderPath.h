#pragma once

#include "RenderPath.h"

namespace Chimera
{
    class HybridRenderPath : public RenderPath
    {
    public:
        HybridRenderPath(VulkanContext& context, std::shared_ptr<Scene> scene);
        virtual ~HybridRenderPath();

        virtual void Render(const RenderFrameInfo& frameInfo) override;
        virtual RenderPathType GetType() const override { return RenderPathType::Hybrid; }
    };
}