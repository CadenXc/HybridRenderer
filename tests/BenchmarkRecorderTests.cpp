#include "Renderer/Benchmark/BenchmarkRecorder.h"
#include "Renderer/Benchmark/BenchmarkCsvWriter.h"

#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
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
    Require(gbuffer.samplesMS.size() == 3,
            "GBuffer should preserve one sample per captured frame");
    RequireNear(gbuffer.samplesMS[0], 1.0,
                "first GBuffer sample is incorrect");
    RequireNear(gbuffer.samplesMS[1], 3.0,
                "second GBuffer sample is incorrect");
    RequireNear(gbuffer.samplesMS[2], 2.0,
                "third GBuffer sample is incorrect");

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
    Require(gbuffer.samplesMS.size() == 3,
            "completed recorder must not retain additional samples");
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
    Require(statistics.samplesMS.size() == 1,
            "repeated pass instances should produce one stored frame sample");
    RequireNear(statistics.samplesMS[0], 4.0,
                "stored frame sample should contain the summed pass cost");
}

std::filesystem::path MakeTemporaryCsvPath()
{
    const auto uniqueId =
        std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("chimera-benchmark-" + std::to_string(uniqueId) + ".csv");
}

std::string ReadTextFile(const std::filesystem::path& path)
{
    std::ifstream file(path);
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

void TestCsvExportRequiresCompletedCapture()
{
    Chimera::BenchmarkRecorder recorder;
    recorder.Start(0, 2);
    recorder.SubmitFrame({{"Pass", 1.0f}});

    const std::filesystem::path outputPath = MakeTemporaryCsvPath();
    const Chimera::BenchmarkCsvResult result =
        Chimera::WriteBenchmarkCsv(recorder, outputPath);

    Require(!result.success,
            "CSV export must reject an incomplete benchmark capture");
    Require(!std::filesystem::exists(outputPath),
            "rejected CSV export must not create an output file");
}

void TestCsvExportIsSortedAndEscaped()
{
    Chimera::BenchmarkRecorder recorder;
    recorder.Start(0, 1);
    recorder.SubmitFrame({{"ZPass", 3.0f},
                          {"APass", 1.0f},
                          {"Pass, \"Quoted\"", 2.0f}});

    const std::filesystem::path outputPath = MakeTemporaryCsvPath();
    const Chimera::BenchmarkCsvResult result =
        Chimera::WriteBenchmarkCsv(recorder, outputPath);

    Require(result.success, "completed benchmark should export to CSV");

    const std::string csv = ReadTextFile(outputPath);
    const size_t aPosition = csv.find("APass,1,1.000000");
    const size_t escapedPosition = csv.find("\"Pass, \"\"Quoted\"\"\"");
    const size_t zPosition = csv.find("ZPass,1,3.000000");

    Require(csv.starts_with(
                "pass,samples,average_ms,min_ms,max_ms,total_ms\n"),
            "CSV export must contain the expected header");
    Require(aPosition != std::string::npos &&
                escapedPosition != std::string::npos &&
                zPosition != std::string::npos,
            "CSV export must contain every recorded pass");
    Require(aPosition < escapedPosition && escapedPosition < zPosition,
            "CSV rows must be sorted by pass name");

    std::error_code removeError;
    std::filesystem::remove(outputPath, removeError);
}

void TestCsvExportReportsDirectoryCreationFailure()
{
    Chimera::BenchmarkRecorder recorder;
    recorder.Start(0, 1);
    recorder.SubmitFrame({{"Pass", 1.0f}});

    const std::filesystem::path parentFile = MakeTemporaryCsvPath();
    {
        std::ofstream file(parentFile);
        Require(file.is_open(), "test must create its parent-path blocker");
    }

    const Chimera::BenchmarkCsvResult result = Chimera::WriteBenchmarkCsv(
        recorder, parentFile / "benchmark.csv");

    Require(!result.success,
            "CSV export must report output directory creation failure");
    Require(!result.error.empty(),
            "CSV export failure must contain a diagnostic message");

    std::error_code removeError;
    std::filesystem::remove(parentFile, removeError);
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

        TestCsvExportRequiresCompletedCapture();
        std::cout << "[PASS] CSV export rejects incomplete capture\n";

        TestCsvExportIsSortedAndEscaped();
        std::cout << "[PASS] CSV export is sorted and escaped\n";

        TestCsvExportReportsDirectoryCreationFailure();
        std::cout << "[PASS] CSV export reports directory failure\n";

        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
