#pragma once

#include "Scene/SceneCommon.h"
#include <string>
#include <memory>

namespace Chimera {

    class ResourceManager;

    class AssetImporter
    {
    public:
        static std::shared_ptr<ImportedScene> ImportScene(const std::string& path, ResourceManager* resourceManager);

    private:
        static void LoadGLTF(const std::string& path, ImportedScene& outScene, ResourceManager* resourceManager);
        static void LoadOBJ(const std::string& path, ImportedScene& outScene, ResourceManager* resourceManager);
    };

}
