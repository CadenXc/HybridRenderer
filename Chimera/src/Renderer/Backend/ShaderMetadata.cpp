#include "pch.h"
#include "ShaderMetadata.h"
#include "ShaderManager.h"

namespace Chimera
{
    const ShaderLayout& ShaderLibrary::GetMergedLayout(const std::string& name, const std::vector<std::string>& shaderNames)
    {
        if (s_Layouts.count(name))
        {
            return s_Layouts[name];
        }

        ShaderLayout merged;
        merged.name = name;

        for (const auto& sName : shaderNames)
        {
            auto shader = ShaderManager::GetShader(sName);
            merged.Merge(shader->GetLayout());
        }

        s_Layouts[name] = merged;
        return s_Layouts[name];
    }
}
