#include "pch.h"
#include "BenchmarkCsvWriter.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <vector>

namespace Chimera
{
namespace
{
std::string EscapeCsvField(const std::string& value)
{
    if (value.find_first_of(",\"\r\n") == std::string::npos)
    {
        return value;
    }

    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');

    for (char character : value)
    {
        if (character == '"') escaped.push_back('"');
        escaped.push_back(character);
    }

    escaped.push_back('"');
    return escaped;
}
} // namespace

BenchmarkCsvResult WriteBenchmarkCsv(
    const BenchmarkRecorder& recorder,
    const std::filesystem::path& outputPath)
{
    BenchmarkCsvResult result;
    result.path = outputPath;

    if (!recorder.IsComplete())
    {
        result.error = "benchmark capture is not complete";
        return result;
    }

    if (recorder.GetStatistics().empty())
    {
        result.error = "benchmark contains no timing samples";
        return result;
    }

    if (outputPath.empty())
    {
        result.error = "output path is empty";
        return result;
    }

    const std::filesystem::path parentPath = outputPath.parent_path();
    if (!parentPath.empty())
    {
        std::error_code directoryError;
        std::filesystem::create_directories(parentPath, directoryError);
        if (directoryError)
        {
            result.error = "failed to create output directory: " +
                           directoryError.message();
            return result;
        }
    }

    std::ofstream file(outputPath, std::ios::out | std::ios::trunc);
    if (!file.is_open())
    {
        result.error = "failed to open output file";
        return result;
    }

    std::vector<const PassTimingStatistics*> sortedStatistics;
    sortedStatistics.reserve(recorder.GetStatistics().size());

    for (const auto& [name, statistics] : recorder.GetStatistics())
    {
        sortedStatistics.push_back(&statistics);
    }

    std::sort(sortedStatistics.begin(), sortedStatistics.end(),
              [](const PassTimingStatistics* lhs,
                 const PassTimingStatistics* rhs)
              {
                  return lhs->name < rhs->name;
              });

    file << "pass,samples,average_ms,min_ms,max_ms,total_ms\n";
    file << std::fixed << std::setprecision(6);

    for (const PassTimingStatistics* statistics : sortedStatistics)
    {
        file << EscapeCsvField(statistics->name) << ','
             << statistics->sampleCount << ','
             << statistics->GetAverageMS() << ',' << statistics->minMS << ','
             << statistics->maxMS << ',' << statistics->totalMS << '\n';
    }

    file.flush();
    if (!file.good())
    {
        result.error = "failed while writing output file";
        return result;
    }

    result.success = true;
    return result;
}
} // namespace Chimera
