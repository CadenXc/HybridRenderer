#include "Renderer/Benchmark/BenchmarkRecorder.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
void Require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

void RequireNear(double actual, double expected, const std::string& message)
{
    constexpr double epsilon = 0.0001;
    Require(std::abs(actual - expected) <= epsilon, message);
}

void TestWarmupAndCaptureLifecycle()
{
    Chimera::BenchmarkRecorder recorder;
    recorder.Start(2, 3);

    Require(recorder.IsRunning(), "recorder should run after Start");
    Require(!recorder.IsComplete(), "recorder should not start completed");

    recorder.SubmitFrame({});
    Require(recorder.GetWarmupFramesRemaining() == 2,
            "empty timing data must not consume a warmup frame");

    recorder.SubmitFrame({{"GBuffer", 100.0f}});
    recorder.SubmitFrame({{"GBuffer", 100.0f}});

    Require(recorder.GetWarmupFramesRemaining() == 0,
            "valid timing frames should consume warmup frames");
    Require(recorder.GetStatistics().empty(),
            "warmup timings must not enter statistics");

    recorder.SubmitFrame({{"GBuffer", 1.0f}, {"RTShadow", 2.0f}});
    recorder.SubmitFrame({{"GBuffer", 3.0f}, {"RTShadow", 4.0f}});
    recorder.SubmitFrame({{"GBuffer", 2.0f}, {"RTShadow", 6.0f}});

    Require(!recorder.IsRunning(),
            "recorder should stop after the target capture count");
    Require(recorder.IsComplete(),
            "recorder should report completion after capture");
    Require(recorder.GetCapturedFrameCount() == 3,
            "recorder should count captured frames once per submission");

    const auto& statistics = recorder.GetStatistics();
    Require(statistics.size() == 2,
            "recorder should keep independent statistics per pass");

    const auto& gbuffer = statistics.at("GBuffer");
    Require(gbuffer.sampleCount == 3,
            "GBuffer should contain one sample per captured frame");
    RequireNear(gbuffer.minMS, 1.0, "GBuffer minimum is incorrect");
    RequireNear(gbuffer.maxMS, 3.0, "GBuffer maximum is incorrect");
    RequireNear(gbuffer.GetAverageMS(), 2.0,
                "GBuffer average is incorrect");

    const auto& shadow = statistics.at("RTShadow");
    Require(shadow.sampleCount == 3,
            "RTShadow should contain one sample per captured frame");
    RequireNear(shadow.minMS, 2.0, "RTShadow minimum is incorrect");
    RequireNear(shadow.maxMS, 6.0, "RTShadow maximum is incorrect");
    RequireNear(shadow.GetAverageMS(), 4.0,
                "RTShadow average is incorrect");

    recorder.SubmitFrame({{"GBuffer", 999.0f}});
    Require(gbuffer.sampleCount == 3,
            "completed recorder must ignore additional frames");
    RequireNear(gbuffer.maxMS, 3.0,
                "completed recorder must freeze its statistics");
}

void TestResetAndZeroLengthCapture()
{
    Chimera::BenchmarkRecorder recorder;
    recorder.Start(0, 1);
    recorder.SubmitFrame({{"Pass", 1.0f}});

    recorder.Reset();

    Require(!recorder.IsRunning(), "Reset should stop the recorder");
    Require(!recorder.IsComplete(),
            "Reset should return the recorder to an idle state");
    Require(recorder.GetCapturedFrameCount() == 0,
            "Reset should clear the captured frame count");
    Require(recorder.GetStatistics().empty(),
            "Reset should clear accumulated statistics");

    recorder.Start(5, 0);
    Require(!recorder.IsRunning(),
            "zero-length capture should not enter the running state");
    Require(recorder.IsComplete(),
            "zero-length capture should complete immediately");
}

void TestDuplicatePassNamesAreSummedPerFrame()
{
    Chimera::BenchmarkRecorder recorder;
    recorder.Start(0, 1);

    recorder.SubmitFrame({{"SVGFAtrousPass", 1.25f},
                          {"SVGFAtrousPass", 2.75f}});

    const auto& statistics = recorder.GetStatistics().at("SVGFAtrousPass");
    Require(statistics.sampleCount == 1,
            "repeated pass instances should produce one sample per frame");
    RequireNear(statistics.minMS, 4.0,
                "repeated pass instances should be summed for frame minimum");
    RequireNear(statistics.maxMS, 4.0,
                "repeated pass instances should be summed for frame maximum");
    RequireNear(statistics.GetAverageMS(), 4.0,
                "repeated pass instances should contribute total frame cost");
}
} // namespace

int main()
{
    try
    {
        TestWarmupAndCaptureLifecycle();
        std::cout << "[PASS] benchmark warmup and capture lifecycle\n";

        TestResetAndZeroLengthCapture();
        std::cout << "[PASS] benchmark reset and zero-length capture\n";

        TestDuplicatePassNamesAreSummedPerFrame();
        std::cout << "[PASS] duplicate pass names are summed per frame\n";

        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
