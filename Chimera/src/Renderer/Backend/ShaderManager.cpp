#include "pch.h"
#include "ShaderManager.h"
#include "Core/Log.h"
#include "Core/EngineConfig.h"
#include <fstream>

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
        // 1. Check cache first
        if (s_ShaderCache.count(name))
        {
            return s_ShaderCache[name];
        }

        // 2. Resolve alias if it exists
        std::string actualPath = name;
        if (s_AliasMap.count(name))
        {
            actualPath = s_AliasMap[name];
        }

        // 3. Construct final SPV path
        std::string fullPath = Config::SHADER_DIR + actualPath + ".spv";
        CH_CORE_INFO("ShaderManager: Loading shader '{0}' (Path: {1})", name, fullPath);

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
