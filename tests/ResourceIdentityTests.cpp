#include "Renderer/Resources/ResourceManager.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void Require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

void TestTextureColorSpaceIsPartOfCacheIdentity()
{
    const std::string path = "assets/shared-texture.png";
    const std::string colorKey = Chimera::MakeTextureCacheKey(path, true);
    const std::string dataKey = Chimera::MakeTextureCacheKey(path, false);

    Require(colorKey != dataKey,
            "sRGB and linear interpretations must have different cache keys");
    Require(colorKey == Chimera::MakeTextureCacheKey(path, true),
            "texture cache keys must be deterministic");
    Require(colorKey !=
                Chimera::MakeTextureCacheKey("assets/other-texture.png", true),
            "different texture paths must have different cache keys");
}
} // namespace

int main()
{
    try
    {
        TestTextureColorSpaceIsPartOfCacheIdentity();
        std::cout << "[PASS] texture cache identity includes color space\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
