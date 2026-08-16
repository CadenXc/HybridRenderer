#include "pch.h"
#include "ImageComparison.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

#include <stb_image.h>
#include <stb_image_write.h>

namespace Chimera
{
namespace
{
struct LoadedPngPair
{
    std::vector<uint8_t> reference;
    std::vector<uint8_t> actual;
    uint32_t width = 0;
    uint32_t height = 0;
    std::string error;
};

LoadedPngPair LoadPngPair(const std::string& referencePath,
                          const std::string& actualPath)
{
    LoadedPngPair pair;
    using StbiPixels = std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>;

    int referenceWidth = 0;
    int referenceHeight = 0;
    int referenceChannels = 0;
    StbiPixels referencePixels(
        stbi_load(referencePath.c_str(), &referenceWidth, &referenceHeight,
                  &referenceChannels, STBI_rgb_alpha),
        stbi_image_free);
    if (!referencePixels)
    {
        pair.error = "failed to load reference image: " + referencePath;
        return pair;
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
        pair.error = "failed to load actual image: " + actualPath;
        return pair;
    }

    if (referenceWidth != actualWidth || referenceHeight != actualHeight)
    {
        pair.error = "image dimensions do not match";
        return pair;
    }

    pair.width = static_cast<uint32_t>(referenceWidth);
    pair.height = static_cast<uint32_t>(referenceHeight);
    const size_t byteCount = static_cast<size_t>(pair.width) *
                             static_cast<size_t>(pair.height) * 4;
    pair.reference.assign(referencePixels.get(),
                          referencePixels.get() + byteCount);
    pair.actual.assign(actualPixels.get(), actualPixels.get() + byteCount);
    return pair;
}
} // namespace

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
    const LoadedPngPair pair = LoadPngPair(referencePath, actualPath);
    if (!pair.error.empty())
    {
        ImageComparisonResult result;
        result.error = pair.error;
        return result;
    }

    return CompareRgba8(pair.reference, pair.actual, pair.width, pair.height,
                        channelThreshold);
}

ImageComparisonResult ComparePngFilesAndWriteDifference(
    const std::string& referencePath, const std::string& actualPath,
    const std::string& differenceOutputPath, uint8_t channelThreshold,
    uint8_t differenceAmplification)
{
    const LoadedPngPair pair = LoadPngPair(referencePath, actualPath);
    if (!pair.error.empty())
    {
        ImageComparisonResult result;
        result.error = pair.error;
        return result;
    }

    ImageComparisonResult result = CompareRgba8(
        pair.reference, pair.actual, pair.width, pair.height,
        channelThreshold);
    if (!result.success)
    {
        return result;
    }

    const size_t pixelCount = static_cast<size_t>(pair.width) * pair.height;
    std::vector<uint8_t> differencePixels(pixelCount * 3, 0);
    for (size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex)
    {
        const size_t sourceOffset = pixelIndex * 4;
        const int alphaDifference = std::abs(
            static_cast<int>(pair.actual[sourceOffset + 3]) -
            static_cast<int>(pair.reference[sourceOffset + 3]));

        for (size_t channel = 0; channel < 3; ++channel)
        {
            int difference = std::abs(
                static_cast<int>(pair.actual[sourceOffset + channel]) -
                static_cast<int>(pair.reference[sourceOffset + channel]));
            difference = std::max(difference, alphaDifference);
            if (difference <= channelThreshold)
            {
                difference = 0;
            }

            const int amplified = difference * differenceAmplification;
            differencePixels[pixelIndex * 3 + channel] =
                static_cast<uint8_t>(std::min(amplified, 255));
        }
    }

    const int writeSucceeded = stbi_write_png(
        differenceOutputPath.c_str(), static_cast<int>(pair.width),
        static_cast<int>(pair.height), 3, differencePixels.data(),
        static_cast<int>(pair.width * 3));
    if (writeSucceeded == 0)
    {
        result.success = false;
        result.error =
            "failed to write difference image: " + differenceOutputPath;
    }

    return result;
}
} // namespace Chimera
