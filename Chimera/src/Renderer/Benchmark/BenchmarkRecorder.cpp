#include "pch.h"
#include "BenchmarkRecorder.h"

#include <algorithm>
#include <cmath>

namespace Chimera
{
double PassTimingStatistics::GetAverageMS() const
{
    return sampleCount > 0 ? totalMS / static_cast<double>(sampleCount) : 0.0;
}

double PassTimingStatistics::GetPercentileMS(double percentile) const
{
    if (samplesMS.empty())
    {
        return 0.0;
    }

    percentile = std::clamp(percentile, 0.0, 1.0);

    std::vector<float> sortedSamples = samplesMS;
    std::sort(sortedSamples.begin(), sortedSamples.end());

    if (percentile == 0.0)
    {
        return sortedSamples.front();
    }

    const size_t rank = static_cast<size_t>(
        std::ceil(percentile * static_cast<double>(sortedSamples.size())));
    return sortedSamples[rank - 1];
}

double PassTimingStatistics::GetP50MS() const
{
    return GetPercentileMS(0.50);
}

double PassTimingStatistics::GetP95MS() const
{
    return GetPercentileMS(0.95);
}

void BenchmarkRecorder::Start(uint32_t warmupFrames, uint32_t captureFrames)
{
    Reset();

    m_WarmupFramesRemaining = warmupFrames;
    m_TargetCaptureFrames = captureFrames;
    m_Running = captureFrames > 0;
    m_Complete = captureFrames == 0;
}

void BenchmarkRecorder::SubmitFrame(const std::vector<PassTiming>& timings)
{
    if (!m_Running || timings.empty())
    {
        return;
    }

    if (m_WarmupFramesRemaining > 0)
    {
        --m_WarmupFramesRemaining;
        return;
    }

    std::unordered_map<std::string, float> frameTotals;
    for (const PassTiming& timing : timings)
    {
        frameTotals[timing.name] += timing.durationMS;
    }

    for (const auto& [name, durationMS] : frameTotals)
    {
        auto [entry, inserted] = m_Statistics.try_emplace(name);
        PassTimingStatistics& statistics = entry->second;

        if (inserted)
        {
            statistics.name = name;
            statistics.minMS = durationMS;
            statistics.maxMS = durationMS;
        }
        else
        {
            statistics.minMS = std::min(statistics.minMS, durationMS);
            statistics.maxMS = std::max(statistics.maxMS, durationMS);
        }

        ++statistics.sampleCount;
        statistics.totalMS += static_cast<double>(durationMS);
        statistics.samplesMS.push_back(durationMS);
    }

    ++m_CapturedFrames;

    if (m_CapturedFrames >= m_TargetCaptureFrames)
    {
        m_Running = false;
        m_Complete = true;
    }
}

void BenchmarkRecorder::Reset()
{
    m_WarmupFramesRemaining = 0;
    m_TargetCaptureFrames = 0;
    m_CapturedFrames = 0;
    m_Running = false;
    m_Complete = false;
    m_Statistics.clear();
}
} // namespace Chimera
