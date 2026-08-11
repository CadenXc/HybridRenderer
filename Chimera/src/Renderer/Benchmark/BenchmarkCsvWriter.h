#pragma once

#include "BenchmarkRecorder.h"

#include <filesystem>
#include <string>

namespace Chimera
{
struct BenchmarkCsvMetadata
{
    std::string gpuName;
    std::string renderPath;
    std::string scenePreset;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t renderFlags = 0;
};

struct BenchmarkCsvResult
{
    bool success = false;
    std::filesystem::path path;
    std::string error;
};

BenchmarkCsvResult WriteBenchmarkCsv(
    const BenchmarkRecorder& recorder,
    const BenchmarkCsvMetadata& metadata,
    const std::filesystem::path& outputPath);
} // namespace Chimera
