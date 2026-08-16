#pragma once

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
