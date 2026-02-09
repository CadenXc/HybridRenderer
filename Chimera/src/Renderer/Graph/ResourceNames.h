#pragma once
#include <string>

namespace Chimera
{
    namespace RS
    {
        // --- 系统保留 ---
        inline static const std::string RENDER_OUTPUT = "RENDER_OUTPUT";

        // --- 核心资源 (C++ 与 Shader 共用此名称) ---
        inline static const std::string Albedo        = "Albedo";
        inline static const std::string Normal        = "Normal";
        inline static const std::string Material      = "Material";
        inline static const std::string Motion        = "Motion";
        inline static const std::string Depth         = "Depth";

        // --- 场景数据 ---
        inline static const std::string SceneAS       = "SceneAS";
        inline static const std::string MaterialBuffer = "MaterialBuffer";
        inline static const std::string InstanceBuffer = "InstanceBuffer";
        inline static const std::string TextureArray   = "TextureArray";

        // --- 光追与中间件 ---
        inline static const std::string RTOutput      = "RTOutput";
        inline static const std::string ShadowAO      = "ShadowAO";
        inline static const std::string Reflections   = "Reflections";
        
        // --- SVGF / 降噪 ---
        inline static const std::string SVGFOutput    = "SVGFOutput";
        inline static const std::string PrevNormal    = "PrevNormal";
        inline static const std::string PrevDepth     = "PrevDepth";
        inline static const std::string History       = "History";
        inline static const std::string Moments       = "Moments";

        // --- 后处理 ---
        inline static const std::string AtrousPing    = "AtrousPing";
        inline static const std::string AtrousPong    = "AtrousPong";
        inline static const std::string FinalColor    = "FinalColor";
        inline static const std::string LinearDepth   = "LinearDepth";

        // Compatibility aliases
        inline static const std::string FINAL_COLOR   = FinalColor;
        inline static const std::string DEPTH         = Depth;
        inline static const std::string FORWARD_COLOR = FinalColor; // Assuming Forward writes to FinalColor
    }
}
