#pragma once

#include "Renderer/Graph/RenderGraphPass.h"
#include <glm/glm.hpp>

namespace Chimera {

    class SVGFPass : public RenderGraphPass {
    public:
        struct SVGFPushConstants {
            glm::uvec2 integrated_shadow_and_ao; // indices or handled by graph?
            uint32_t prev_frame_normals_and_object_ids;
            uint32_t shadow_and_ao_history;
            uint32_t shadow_and_ao_moments_history;
            int atrous_step;
        };

        SVGFPass();
        void Setup(RenderGraph& graph);
    };

}
