#pragma once
#include "pch.h"
#include "Shader.h"
#include <filesystem>
#include <unordered_map>

namespace Chimera
{
    class ShaderManager
    {
    public:
        static void Init(const std::string& shaderDir, const std::string& sourceDir);

        // Register a friendly name for a shader path
        static void RegisterAlias(const std::string& alias, const std::string& path);

        // Returns a shader object containing bytecode and reflection data
        static std::shared_ptr<Shader> GetShader(const std::string& name);

        static bool CheckForUpdates();
        static void RecompileAll();

        static void ClearCache()
        {
            s_ShaderCache.clear();
            s_Timestamps.clear();
            s_AliasMap.clear();
        }

    private:
        inline static std::string s_ShaderDir;
        inline static std::string s_SourceDir;
        inline static std::unordered_map<std::string, std::string> s_AliasMap; // Friendly Name -> Actual Path
        inline static std::unordered_map<std::string, std::filesystem::file_time_type> s_Timestamps;
        inline static std::unordered_map<std::string, std::shared_ptr<Shader>> s_ShaderCache;
    };
}
