#pragma once
#include <string>
#include <filesystem>

namespace Chimera
{

    class Config
    {
    public:
        // 基础路径配置
        inline static std::string SHADER_DIR = "shaders/";
        inline static std::string ASSET_DIR  = "assets/";
        
        // 源码路径（用于热重载）
        inline static std::string SHADER_SOURCE_DIR = "../../../Chimera/shaders";

        static void Init()
        {
            std::filesystem::path currentPath = std::filesystem::current_path();
            std::filesystem::path root = currentPath;
            bool foundRoot = false;
            for (int i = 0; i < 5; ++i)
            {
                if (std::filesystem::exists(root / "Chimera") && std::filesystem::exists(root / "scripts"))
                {
                    foundRoot = true;
                    break;
                }
                if (!root.has_parent_path()) break;
                root = root.parent_path();
            }

            if (foundRoot)
            {
                SHADER_SOURCE_DIR = (root / "Chimera" / "shaders").string();
                
                // Point SHADER_DIR to the compiled binaries in build folder
                std::filesystem::path compiledDir = root / "build" / "shaders_compiled";
                if (std::filesystem::exists(compiledDir))
                {
                    SHADER_DIR = compiledDir.string() + "/";
                }
            }
        }

        // 全局引擎设置
        struct EngineSettings
        {
            float ClearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
            bool EnableHotReload = true;
            float HotReloadCheckInterval = 1.0f;

            // Debug Settings
            int DisplayMode = 0; // 0: Final, 1: Shadow, 2: AO, 3: Reflect

            // Light Settings
            float LightPosition[3] = { 5.0f, 5.0f, 5.0f };
            float LightColor[3] = { 1.0f, 1.0f, 1.0f };
            float LightIntensity = 10.0f;
        };

        inline static EngineSettings Settings;
    };

}