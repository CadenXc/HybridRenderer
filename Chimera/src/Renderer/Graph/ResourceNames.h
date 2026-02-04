#pragma once
#include <string>

namespace Chimera {

    namespace RS {
        // 保留关键�?
        inline static const std::string RENDER_OUTPUT = "RENDER_OUTPUT";
        inline static const std::string FINAL_COLOR   = "FinalColor";

        // 基础资源
        inline static const std::string ALBEDO        = "Albedo";
        inline static const std::string FORWARD_COLOR = "ForwardColor";
        inline static const std::string NORMAL        = "Normal";
        inline static const std::string MATERIAL = "Material";
        inline static const std::string MOTION   = "Motion";
        inline static const std::string DEPTH    = "Depth";

        // 光追资源
        inline static const std::string RT_OUTPUT = "RT_Output";
        inline static const std::string SCENE_AS  = "SceneTLAS";
    }

}
