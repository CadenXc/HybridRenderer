#include "pch.h"
#include "ShaderManager.h"
#include "Core/Log.h"
#include "Core/EngineConfig.h"
#include <fstream>
#include <filesystem>

namespace Chimera
{
    void ShaderManager::Init(const std::string& shaderDir, const std::string& sourceDir)
    {
        s_ShaderDir = shaderDir;
        s_SourceDir = sourceDir;
    }

    void ShaderManager::RegisterAlias(const std::string& alias, const std::string& path)
    {
        s_AliasMap[alias] = path;
        CH_CORE_TRACE("ShaderManager: Registered alias '{0}' -> '{1}'", alias, path);
    }

    std::shared_ptr<Shader> ShaderManager::GetShader(const std::string& name)
    {
        if (s_ShaderCache.count(name))
        {
            return s_ShaderCache[name];
        }

        std::string actualPath = name;
        if (s_AliasMap.count(name))
        {
            actualPath = s_AliasMap[name];
        }

        // --- ULTRA ROBUST PATH RESOLUTION ---
        std::filesystem::path baseDir = Config::SHADER_DIR;
        std::filesystem::path shaderFile = actualPath + ".spv";
        
        // Use operator / for proper path joining
        std::filesystem::path fullPath = baseDir / shaderFile;
        
        // If not found in Config dir, try local shaders folder
        if (!std::filesystem::exists(fullPath))
        {
            fullPath = std::filesystem::path("shaders") / shaderFile;
        }

        // 1. Convert to absolute path to rule out working directory issues
        // 2. Normalize separators (\ vs /) for Windows stability
        fullPath = std::filesystem::absolute(fullPath).make_preferred();

        CH_CORE_INFO("ShaderManager: Loading shader '{0}' from [ {1} ]", name, fullPath.string());

        // Pass the fully normalized absolute path
        auto shader = std::make_shared<Shader>(fullPath);
        s_ShaderCache[name] = shader;
        return shader;
    }

    bool ShaderManager::CheckForUpdates()
    {
        return false;
    }

    void ShaderManager::RecompileAll()
    {
        CH_CORE_INFO("ShaderManager: Recompiling all shaders...");
    }
}
