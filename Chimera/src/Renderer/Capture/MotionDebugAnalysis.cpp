#include "pch.h"
#include "MotionDebugAnalysis.h"

#include <limits>
#include <memory>

#include <stb_image.h>

namespace Chimera
{
MotionDebugStatistics AnalyzeMotionDebugRgba8(
    const std::vector<uint8_t>& pixels, uint32_t width, uint32_t height,
    uint8_t dominanceMargin)
{
    MotionDebugStatistics result;
    result.width = width;
    result.height = height;

    if (width == 0 || height == 0)
    {
        result.error = "image dimensions must be non-zero";
        return result;
    }

    const uint64_t pixelCount = static_cast<uint64_t>(width) * height;
    if (pixelCount > std::numeric_limits<size_t>::max() / 4 ||
        pixels.size() != static_cast<size_t>(pixelCount * 4))
    {
        result.error = "RGBA8 byte count does not match image dimensions";
        return result;
    }

    const int margin = static_cast<int>(dominanceMargin);
    for (uint64_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex)
    {
        const size_t offset = static_cast<size_t>(pixelIndex * 4);
        const int red = pixels[offset + 0];
        const int green = pixels[offset + 1];
        const int blue = pixels[offset + 2];

        if (red >= green + margin && red >= blue + margin)
        {
            ++result.horizontalMotionPixelCount;
        }
        else if (green >= red + margin && green >= blue + margin)
        {
            ++result.verticalMotionPixelCount;
        }
        else
        {
            ++result.neutralPixelCount;
        }
    }

    result.success = true;
    return result;
}

MotionDebugStatistics AnalyzeMotionDebugPng(
    const std::string& path, uint8_t dominanceMargin)
{
    using StbiPixels = std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>;

    int width = 0;
    int height = 0;
    int channelCount = 0;
    StbiPixels pixels(stbi_load(path.c_str(), &width, &height, &channelCount,
                                STBI_rgb_alpha),
                      stbi_image_free);
    if (!pixels)
    {
        MotionDebugStatistics result;
        result.error = "failed to load motion debug image: " + path;
        return result;
    }

    if (width <= 0 || height <= 0)
    {
        MotionDebugStatistics result;
        result.error = "motion debug image dimensions must be positive";
        return result;
    }

    const size_t byteCount = static_cast<size_t>(width) *
                             static_cast<size_t>(height) * 4;
    const std::vector<uint8_t> rgba(pixels.get(), pixels.get() + byteCount);
    return AnalyzeMotionDebugRgba8(rgba, static_cast<uint32_t>(width),
                                   static_cast<uint32_t>(height),
                                   dominanceMargin);
}
} // namespace Chimera
