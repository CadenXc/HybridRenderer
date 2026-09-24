#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Chimera
{
struct TemporalHistoryDebugStatistics
{
    bool success = false;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t acceptedPixelCount = 0;
    uint64_t rejectedPixelCount = 0;
    uint64_t unclassifiedPixelCount = 0;
    std::string error;

    uint64_t GetClassifiedPixelCount() const
    {
        return acceptedPixelCount + rejectedPixelCount;
    }
};

TemporalHistoryDebugStatistics AnalyzeTemporalHistoryRgba8(
    const std::vector<uint8_t>& pixels, uint32_t width, uint32_t height,
    uint8_t dominanceMargin = 32);

TemporalHistoryDebugStatistics AnalyzeTemporalHistoryPng(
    const std::string& path, uint8_t dominanceMargin = 32);
} // namespace Chimera
