#pragma once
#include "Renderer/Graph/RenderGraph.h"

namespace Chimera {

    class RTShadowAOPass {
    public:
        RTShadowAOPass(std::shared_ptr<class VulkanContext> context, uint32_t width, uint32_t height)
            : m_Context(context), m_Width(width), m_Height(height) {}

        void Setup(RenderGraph& graph);

    private:
        std::shared_ptr<class VulkanContext> m_Context;
        uint32_t m_Width, m_Height;
    };

}