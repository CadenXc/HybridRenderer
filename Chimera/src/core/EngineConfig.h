#pragma once
#include <string>
#include <filesystem>

namespace Chimera {

    class Config
    {
    public:
        // 基础路径配置
        inline static const std::string SHADER_DIR = "shaders/";
        inline static const std::string ASSET_DIR  = "assets/";
        
        // 源码路径（用于热重载�?
        // 运行目录: build/Sandbox/Debug/
        // 目标路径: Chimera/shaders/
        inline static const std::string SHADER_SOURCE_DIR = "../../../Chimera/shaders";

        // 全局引擎设置
        struct EngineSettings {
            float ClearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
            bool EnableHotReload = true;
            float HotReloadCheckInterval = 1.0f;
        };

        inline static EngineSettings Settings;
    };

}
