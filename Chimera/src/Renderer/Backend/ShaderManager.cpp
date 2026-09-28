#include "pch.h"
#include "ShaderManager.h"
#include "Core/Log.h"
#include "Core/Application.h"
#include <filesystem>

namespace Chimera
{
void ShaderManager::RegisterAlias(const std::string& alias,
                                  const std::string& path)
{
    s_AliasMap[alias] = path;
    CH_CORE_TRACE("ShaderManager: Registered alias '{0}' -> '{1}'", alias,
                  path);
}

std::shared_ptr<Shader> ShaderManager::GetShader(const std::string& name)
{
    std::string actualPath = name;
    if (const auto alias = s_AliasMap.find(name); alias != s_AliasMap.end())
    {
        actualPath = alias->second;
    }

        // --- ULTRA ROBUST PATH RESOLUTION ---
    std::filesystem::path baseDir =
        Application::Get().GetSpecification().ShaderDir;
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
    fullPath = std::filesystem::absolute(fullPath)
                   .lexically_normal()
                   .make_preferred();

    const std::string cacheKey = fullPath.string();
    if (const auto cached = s_ShaderCache.find(cacheKey);
        cached != s_ShaderCache.end())
    {
        return cached->second;
    }

    CH_CORE_INFO("ShaderManager: Loading shader '{0}' from [ {1} ]", name,
                 fullPath.string());

        // Pass the fully normalized absolute path
    auto shader = std::make_shared<Shader>(fullPath);
    s_ShaderCache.emplace(cacheKey, shader);
    return shader;
}
} // namespace Chimera
