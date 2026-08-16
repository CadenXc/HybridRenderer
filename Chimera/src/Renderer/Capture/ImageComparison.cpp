#include "pch.h"
#include "ImageComparison.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

#include <stb_image.h>

namespace Chimera
{
ImageComparisonResult CompareRgba8(
    const std::vector<uint8_t>& referencePixels,
    const std::vector<uint8_t>& actualPixels, uint32_t width,
    uint32_t height, uint8_t channelThreshold)
{
    ImageComparisonResult result;

    if (width == 0 || height == 0)
    {
        result.error = "image dimensions must be greater than zero";
        return result;
    }

    const uint64_t expectedByteCount64 =
        static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4;

    if (expectedByteCount64 > std::numeric_limits<size_t>::max())
    {
        result.error = "image dimensions exceed addressable memory";
        return result;
    }

    const size_t expectedByteCount =
        static_cast<size_t>(expectedByteCount64);

    if (referencePixels.size() != expectedByteCount)
    {
        result.error =
            "reference pixel count does not match dimensions";
        return result;
    }

    if (actualPixels.size() != expectedByteCount)
    {
        result.error = "actual pixel count does not match dimensions";
        return result;
    }

    double squaredErrorSum = 0.0;
    const size_t pixelCount =
        static_cast<size_t>(width) * static_cast<size_t>(height);

    for (size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex)
    {
        const size_t pixelOffset = pixelIndex * 4;
        bool pixelIsDifferent = false;

        for (size_t channel = 0; channel < 4; ++channel)
        {
            const int referenceValue =
                referencePixels[pixelOffset + channel];
            const int actualValue = actualPixels[pixelOffset + channel];
            const int difference =
                std::abs(actualValue - referenceValue);

            squaredErrorSum += static_cast<double>(difference) *
                               static_cast<double>(difference);

            result.maxChannelDifference =
                std::max(result.maxChannelDifference,
                         static_cast<uint8_t>(difference));

            if (difference > channelThreshold)
            {
                pixelIsDifferent = true;
            }
        }

        if (pixelIsDifferent)
        {
            ++result.differentPixelCount;
        }
    }

    result.rmse =
        std::sqrt(squaredErrorSum / static_cast<double>(expectedByteCount));
    result.success = true;
    return result;
}

ImageComparisonResult ComparePngFiles(
    const std::string& referencePath, const std::string& actualPath,
    uint8_t channelThreshold)
{
    ImageComparisonResult result;

    int referenceWidth = 0;
    int referenceHeight = 0;
    int referenceChannels = 0;
    using StbiPixels = std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>;
    StbiPixels referencePixels(
        stbi_load(referencePath.c_str(), &referenceWidth, &referenceHeight,
                  &referenceChannels, STBI_rgb_alpha),
        stbi_image_free);

    if (!referencePixels)
    {
        result.error = "failed to load reference image: " + referencePath;
        return result;
    }

    int actualWidth = 0;
    int actualHeight = 0;
    int actualChannels = 0;
    StbiPixels actualPixels(
        stbi_load(actualPath.c_str(), &actualWidth, &actualHeight,
                  &actualChannels, STBI_rgb_alpha),
        stbi_image_free);

    if (!actualPixels)
    {
        result.error = "failed to load actual image: " + actualPath;
        return result;
    }

    if (referenceWidth != actualWidth || referenceHeight != actualHeight)
    {
        result.error = "image dimensions do not match";
        return result;
    }

    const size_t byteCount = static_cast<size_t>(referenceWidth) *
                             static_cast<size_t>(referenceHeight) * 4;
    const std::vector<uint8_t> reference(
        referencePixels.get(), referencePixels.get() + byteCount);
    const std::vector<uint8_t> actual(actualPixels.get(),
                                      actualPixels.get() + byteCount);

    return CompareRgba8(reference, actual,
                        static_cast<uint32_t>(referenceWidth),
                        static_cast<uint32_t>(referenceHeight),
                        channelThreshold);
}
} // namespace Chimera
