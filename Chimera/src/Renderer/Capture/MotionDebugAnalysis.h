#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Chimera
{
struct MotionDebugStatistics
{
    bool success = false;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t horizontalMotionPixelCount = 0;
    uint64_t verticalMotionPixelCount = 0;
    uint64_t neutralPixelCount = 0;
    std::string error;

    uint64_t GetMotionPixelCount() const
    {
        return horizontalMotionPixelCount + verticalMotionPixelCount;
    }
};

MotionDebugStatistics AnalyzeMotionDebugRgba8(
    const std::vector<uint8_t>& pixels, uint32_t width, uint32_t height,
    uint8_t dominanceMargin = 32);

MotionDebugStatistics AnalyzeMotionDebugPng(
    const std::string& path, uint8_t dominanceMargin = 32);
} // namespace Chimera
