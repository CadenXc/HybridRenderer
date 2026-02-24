#pragma once

#include "RenderPath.h"

namespace Chimera
{
    class RayTracedRenderPath : public RenderPath
    {
    public:
        RayTracedRenderPath(VulkanContext& context);
        virtual ~RayTracedRenderPath();

        virtual VkSemaphore Render(const RenderFrameInfo& frameInfo) override;
                virtual RenderPathType GetType() const override { return RenderPathType::RayTracing; }
                
                virtual void OnImGui() override; // [NEW]
        
            private:
                bool m_UseAlphaTest = false;
            };
        }