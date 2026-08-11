#pragma once

#include "Renderer/Graph/RenderGraphCommon.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Chimera
{
struct PassTimingStatistics
{
    std::string name;
    uint32_t sampleCount = 0;
    double totalMS = 0.0;
    float minMS = 0.0f;
    float maxMS = 0.0f;
    std::vector<float> samplesMS;

    double GetAverageMS() const;
    double GetPercentileMS(double percentile) const;
    double GetP50MS() const;
    double GetP95MS() const;
};

class BenchmarkRecorder
{
public:
    void Start(uint32_t warmupFrames, uint32_t captureFrames);
    void SubmitFrame(const std::vector<PassTiming>& timings);
    void Reset();

    bool IsRunning() const
    {
        return m_Running;
    }

    bool IsComplete() const
    {
        return m_Complete;
    }

    uint32_t GetWarmupFramesRemaining() const
    {
        return m_WarmupFramesRemaining;
    }

    uint32_t GetCapturedFrameCount() const
    {
        return m_CapturedFrames;
    }

    const std::unordered_map<std::string, PassTimingStatistics>&
    GetStatistics() const
    {
        return m_Statistics;
    }

private:
    uint32_t m_WarmupFramesRemaining = 0;
    uint32_t m_TargetCaptureFrames = 0;
    uint32_t m_CapturedFrames = 0;
    bool m_Running = false;
    bool m_Complete = false;

    std::unordered_map<std::string, PassTimingStatistics> m_Statistics;
};
} // namespace Chimera
