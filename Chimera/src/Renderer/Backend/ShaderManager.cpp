#include "pch.h"
#include "ShaderManager.h"
#include "Core/Log.h"

namespace Chimera {

    void ShaderManager::Init(const std::string& shaderDir, const std::string& sourceDir)
    {
        s_ShaderDir = shaderDir;
        s_SourceDir = sourceDir;
    }

    std::shared_ptr<Shader> ShaderManager::GetShader(const std::string& name)
    {
        if (s_ShaderCache.count(name)) return s_ShaderCache[name];

        std::string path = s_ShaderDir + name + ".spv";
        CH_CORE_INFO("ShaderManager: Loading shader '{0}' from '{1}'", name, path);
        auto shader = std::make_shared<Shader>(path);
        s_ShaderCache[name] = shader;
        return shader;
    }

    bool ShaderManager::CheckForUpdates()
    {
        // ... (Check for updates logic could be updated to clear cache)
        return false;
    }

    void ShaderManager::RecompileAll()
    {
        s_ShaderCache.clear();
    }

}