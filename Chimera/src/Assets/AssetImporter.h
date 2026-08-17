#pragma once

#include "Scene/SceneCommon.h"
#include <string>
#include <memory>

namespace Chimera
{
float CalculateTangentHandedness(const glm::vec3& normal,
                                 const glm::vec3& tangent,
                                 const glm::vec3& bitangent);

struct AssetInfo
{
    std::string Name;
    std::string Path;
};

class AssetImporter
{
public:
    static std::shared_ptr<ImportedScene> ImportScene(const std::string& path);

    static std::vector<AssetInfo> GetAvailableModels(
        const std::string& rootDirectory);
    static std::vector<AssetInfo> GetAvailableHDRs(
        const std::string& rootDirectory);
};
} // namespace Chimera
