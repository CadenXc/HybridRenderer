#pragma once

#include "Scene/SceneCommon.h"
#include <string>
#include <memory>

namespace Chimera
{
class AssetImporter
{
public:
    static std::shared_ptr<ImportedScene> ImportScene(const std::string& path);
};
} // namespace Chimera