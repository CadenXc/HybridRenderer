#pragma once
#include "Renderer/Graph/RenderGraph.h"

namespace Chimera
{

    class SVGFAtrousPass
    {
    public:
        SVGFAtrousPass(uint32_t width, uint32_t height) : m_Width(width), m_Height(height) {}
        void Setup(RenderGraph& graph);

    private:
        uint32_t m_Width, m_Height;
    };

}