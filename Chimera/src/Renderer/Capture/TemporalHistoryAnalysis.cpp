#include "pch.h"
#include "TemporalHistoryAnalysis.h"

#include <limits>
#include <memory>

#include <stb_image.h>

namespace Chimera
{
TemporalHistoryDebugStatistics AnalyzeTemporalHistoryRgba8(
    const std::vector<uint8_t>& pixels, uint32_t width, uint32_t height,
    uint8_t dominanceMargin)
{
    TemporalHistoryDebugStatistics result;
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

        if (green >= red + margin && green >= blue + margin)
        {
            ++result.acceptedPixelCount;
        }
        else if (red >= green + margin && red >= blue + margin)
        {
            ++result.rejectedPixelCount;
        }
        else
        {
            ++result.unclassifiedPixelCount;
        }
    }

    result.success = true;
    return result;
}

TemporalHistoryDebugStatistics AnalyzeTemporalHistoryPng(
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
        TemporalHistoryDebugStatistics result;
        result.error = "failed to load temporal history image: " + path;
        return result;
    }

    if (width <= 0 || height <= 0)
    {
        TemporalHistoryDebugStatistics result;
        result.error = "temporal history image dimensions must be positive";
        return result;
    }

    const size_t byteCount = static_cast<size_t>(width) *
                             static_cast<size_t>(height) * 4;
    const std::vector<uint8_t> rgba(pixels.get(), pixels.get() + byteCount);
    return AnalyzeTemporalHistoryRgba8(rgba, static_cast<uint32_t>(width),
                                       static_cast<uint32_t>(height),
                                       dominanceMargin);
}
} // namespace Chimera
