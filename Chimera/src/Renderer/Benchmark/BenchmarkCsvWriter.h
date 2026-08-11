#pragma once

#include "BenchmarkRecorder.h"

#include <filesystem>
#include <string>

namespace Chimera
{
struct BenchmarkCsvResult
{
    bool success = false;
    std::filesystem::path path;
    std::string error;
};

BenchmarkCsvResult WriteBenchmarkCsv(
    const BenchmarkRecorder& recorder,
    const std::filesystem::path& outputPath);
} // namespace Chimera
