#pragma once
#include <string>

namespace Chimera {

    namespace Shaders {
        // Graphics
        inline static const std::string GBufferV = "gbuffer.vert";
        inline static const std::string GBufferF = "gbuffer.frag";
        inline static const std::string ForwardV = "shader.vert";
        inline static const std::string ForwardF = "shader.frag";
        inline static const std::string SimpleV  = "simple.vert";
        inline static const std::string SimpleF  = "simple.frag";

        // Ray Tracing
        inline static const std::string RayGen      = "raygen.rgen";
        inline static const std::string Miss        = "miss.rmiss";
        inline static const std::string ShadowMiss  = "shadow.rmiss";
        inline static const std::string ClosestHit  = "closesthit.rchit";
        inline static const std::string RTShadowAO  = "rt_shadow_ao.rgen";

        // Compute / Denoising
        inline static const std::string SVGF        = "svgf.comp";
        inline static const std::string SVGFAtrous  = "svgf_atrous.comp";
        inline static const std::string Bloom       = "bloom.comp";
    }

}
