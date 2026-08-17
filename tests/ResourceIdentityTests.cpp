#include "Renderer/Resources/ResourceManager.h"
#include "Utils/VulkanBarrier.h"

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

void TestSwapchainSrgbFormatClassification()
{
    Require(Chimera::VulkanUtils::IsSRGBFormat(VK_FORMAT_R8G8B8A8_SRGB),
            "R8G8B8A8_SRGB must use hardware sRGB encoding");
    Require(Chimera::VulkanUtils::IsSRGBFormat(VK_FORMAT_B8G8R8A8_SRGB),
            "B8G8R8A8_SRGB must use hardware sRGB encoding");
    Require(!Chimera::VulkanUtils::IsSRGBFormat(VK_FORMAT_B8G8R8A8_UNORM),
            "B8G8R8A8_UNORM must use manual shader sRGB encoding");
}

void TestEmbeddedTextureIdentityIncludesOwningModel()
{
    const std::string first =
        Chimera::MakeEmbeddedTextureIdentity("models/a.glb", "*0");
    const std::string second =
        Chimera::MakeEmbeddedTextureIdentity("models/b.glb", "*0");

    Require(first != second,
            "same embedded texture reference in different GLBs must not collide");
    Require(Chimera::MakeTextureCacheKey(first, true) !=
                Chimera::MakeTextureCacheKey(first, false),
            "embedded texture identity must still include color space");
}
} // namespace

int main()
{
    try
    {
        TestTextureColorSpaceIsPartOfCacheIdentity();
        std::cout << "[PASS] texture cache identity includes color space\n";
        TestSwapchainSrgbFormatClassification();
        std::cout << "[PASS] swapchain sRGB formats select one encoding path\n";
        TestEmbeddedTextureIdentityIncludesOwningModel();
        std::cout << "[PASS] embedded texture identity includes owning model\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
