#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Chimera
{
struct ImageComparisonResult
{
    bool success = false;
    uint64_t differentPixelCount = 0;
    uint8_t maxChannelDifference = 0;
    double rmse = 0.0;
    std::string error;
};

struct ImageRegion
{
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

// Mean absolute difference from the four immediate neighbours, excluding
// the region border. This measures local detail, not perceptual image quality.
struct ImageHighFrequencyResult
{
    bool success = false;
    uint64_t sampleCount = 0;
    double meanAbsoluteResidual = 0.0;
    std::array<double, 3> meanRgb{};
    std::string error;
};

ImageHighFrequencyResult AnalyzeHighFrequencyRgba8(
    const std::vector<uint8_t>& pixels, uint32_t width, uint32_t height,
    ImageRegion region, uint32_t channel = 0);

ImageHighFrequencyResult AnalyzeHighFrequencyPng(
    const std::string& path, ImageRegion region, uint32_t channel = 0);

ImageComparisonResult CompareRgba8(
    const std::vector<uint8_t>& referencePixels,
    const std::vector<uint8_t>& actualPixels, uint32_t width,
    uint32_t height, uint8_t channelThreshold = 0);

ImageComparisonResult ComparePngFiles(
    const std::string& referencePath, const std::string& actualPath,
    uint8_t channelThreshold = 0);

ImageComparisonResult ComparePngFilesAndWriteDifference(
    const std::string& referencePath, const std::string& actualPath,
    const std::string& differenceOutputPath,
    uint8_t channelThreshold = 0,
    uint8_t differenceAmplification = 4);
} // namespace Chimera
