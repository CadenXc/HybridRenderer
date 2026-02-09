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

    std::shared_ptr<Shader> ShaderManager::GetShader(const std::string& name)
    {
        if (s_ShaderCache.count(name))
        {
            return s_ShaderCache[name];
        }

        // Use the initialized shader directory to locate the .spv file
        std::string path = Config::SHADER_DIR + name + ".spv";
        CH_CORE_INFO("ShaderManager: Loading shader '{0}' from '{1}'", name, path);

        auto shader = std::make_shared<Shader>(path);
        s_ShaderCache[name] = shader;
        return shader;
    }

    bool ShaderManager::CheckForUpdates()
    {
        // Simple placeholder for now
        return false;
    }

    void ShaderManager::RecompileAll()
    {
        CH_CORE_INFO("ShaderManager: Recompiling all shaders...");
        // This is usually handled by external scripts in this project
    }
}
