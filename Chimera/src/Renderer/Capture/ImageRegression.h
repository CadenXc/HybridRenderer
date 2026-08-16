#pragma once

#include "ImageComparison.h"

#include <cstdint>
#include <string>

namespace Chimera
{
struct ImageRegressionSettings
{
    uint8_t channelThreshold = 2;
    uint64_t allowedDifferentPixelCount = 0;
    uint8_t allowedMaxChannelDifference = 8;
    double allowedRmse = 1.0;
    uint8_t differenceAmplification = 8;
};

struct ImageRegressionResult
{
    bool success = false;
    bool passed = false;
    ImageComparisonResult comparison;
    std::string differencePath;
    std::string error;
};

ImageRegressionResult RunImageRegression(
    const std::string& baselinePath, const std::string& actualPath,
    const std::string& differencePath,
    const ImageRegressionSettings& settings = {});
} // namespace Chimera
