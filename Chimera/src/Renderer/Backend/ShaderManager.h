#pragma once

#include "pch.h"
#include <filesystem>
#include <unordered_map>

namespace Chimera {

    class ShaderManager {
    public:
        static void Init(const std::string& shaderDir, const std::string& sourceDir);
        static std::vector<uint32_t> GetShaderCode(const std::string& name);
        
        static bool CheckForUpdates(); // Combined SPV and Source check
        static void RecompileAll();

    private:
        static std::vector<uint32_t> LoadSPV(const std::string& path);
        
        inline static std::string s_ShaderDir;
        inline static std::string s_SourceDir;
        inline static std::unordered_map<std::string, std::filesystem::file_time_type> s_Timestamps;
    };

}
