#pragma once
#include <string>

namespace Chimera {

    namespace RS {
        // Reserved Keywords
        inline static const std::string RENDER_OUTPUT = "RENDER_OUTPUT";
        inline static const std::string FINAL_COLOR   = "FinalColor";
        inline static const std::string FORWARD_COLOR = "ForwardColor";

        // G-Buffer (RT0 - RT3 + Depth)
        inline static const std::string ALBEDO        = "Albedo";   // RT0: RGBA8 (Albedo + Alpha/ObjectID)
        inline static const std::string NORMAL        = "Normal";   // RT1: RGBA16F (World Normal)
        inline static const std::string MATERIAL      = "Material"; // RT2: RGBA8 (Roughness, Metallic, etc.)
        inline static const std::string MOTION        = "Motion";   // RT3: RGBA16F (Motion Vectors)
        inline static const std::string DEPTH         = "Depth";    // Depth: D32F

        // Scene Data (Buffers/AS)
        inline static const std::string SCENE_AS        = "SceneTLAS";
        inline static const std::string MATERIAL_BUFFER = "MaterialBuffer";
        inline static const std::string TEXTURE_ARRAY   = "TextureArray";

        // Ray Tracing specific
        inline static const std::string RT_OUTPUT       = "RT_Output";
        inline static const std::string RT_AO           = "RT_AO";
        inline static const std::string RT_SHADOWS      = "RT_Shadows";
        inline static const std::string RT_SHADOW_AO    = "RT_Shadow_AO";
        inline static const std::string RT_REFLECTIONS  = "RT_Reflections";

        // SVGF / Denoising
        inline static const std::string SVGF_OUTPUT     = "SVGF_Output";
        inline static const std::string PREV_NORMAL     = "PrevNormal";
        inline static const std::string PREV_DEPTH      = "PrevDepth";
        inline static const std::string SHADOW_AO_HIST  = "ShadowAOHistory";
        inline static const std::string MOMENTS_HIST    = "MomentsHistory";

        inline static const std::string ATROUS_PING     = "AtrousPing";
        inline static const std::string ATROUS_PONG     = "AtrousPong";

        // Bloom Post-processing
        inline static const std::string BLOOM_BRIGHT    = "BloomBright";
        inline static const std::string BLOOM_BLUR_TMP  = "BloomBlurTmp";
        inline static const std::string BLOOM_FINAL     = "BloomFinal";
    }

}