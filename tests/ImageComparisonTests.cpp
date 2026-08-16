#include "Renderer/Capture/ImageComparison.h"
#include "Renderer/Capture/ImageRegression.h"
#include "Renderer/Capture/FrameWarmupCounter.h"
#include "Renderer/Capture/CaptureReadinessTracker.h"
#include "Scene/EditorCamera.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <stb_image.h>
#include <stb_image_write.h>

namespace
{
void Require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void RequireNear(double actual, double expected, const std::string& message)
{
    constexpr double epsilon = 0.0001;
    if (std::abs(actual - expected) > epsilon)
    {
        throw std::runtime_error(message);
    }
}

class TemporaryPngDirectory
{
public:
    TemporaryPngDirectory()
    {
        const auto suffix = std::chrono::steady_clock::now()
                                .time_since_epoch()
                                .count();
        path = std::filesystem::temp_directory_path() /
               ("chimera-image-comparison-" + std::to_string(suffix));
        std::filesystem::create_directories(path);
    }

    ~TemporaryPngDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }

    std::filesystem::path path;
};

void WritePng(const std::filesystem::path& path, int width, int height,
              const std::vector<uint8_t>& pixels)
{
    const int result = stbi_write_png(path.string().c_str(), width, height, 4,
                                      pixels.data(), width * 4);
    Require(result != 0, "test PNG could not be written");
}

void TestIdenticalPixelsProduceNoDifference()
{
    const std::vector<uint8_t> pixels = {10, 20, 30, 255,
                                         40, 50, 60, 255};

    const Chimera::ImageComparisonResult result =
        Chimera::CompareRgba8(pixels, pixels, 2, 1);

    Require(result.success,
            "identical images should compare successfully");
    Require(result.differentPixelCount == 0,
            "identical images must contain no different pixels");
    Require(result.maxChannelDifference == 0,
            "identical images must have zero maximum difference");
    RequireNear(result.rmse, 0.0,
                "identical images must have zero RMSE");
}

void TestDifferenceMetricsAreCalculated()
{
    const std::vector<uint8_t> reference = {0, 0, 0, 0};
    const std::vector<uint8_t> actual = {3, 4, 0, 0};

    const Chimera::ImageComparisonResult result =
        Chimera::CompareRgba8(reference, actual, 1, 1);

    Require(result.success, "valid images should compare successfully");
    Require(result.differentPixelCount == 1,
            "changed pixel must be counted once");
    Require(result.maxChannelDifference == 4,
            "maximum channel difference is incorrect");
    RequireNear(result.rmse, 2.5, "RMSE is incorrect");
}

void TestThresholdOnlyAffectsPixelCount()
{
    const std::vector<uint8_t> reference = {0, 0, 0, 0};
    const std::vector<uint8_t> actual = {2, 0, 0, 0};

    const Chimera::ImageComparisonResult result =
        Chimera::CompareRgba8(reference, actual, 1, 1, 2);

    Require(result.success, "valid images should compare successfully");
    Require(result.differentPixelCount == 0,
            "difference equal to threshold should be accepted");
    Require(result.maxChannelDifference == 2,
            "maximum difference must ignore threshold filtering");
    RequireNear(result.rmse, 1.0,
                "RMSE must retain differences within threshold");
}

void TestInvalidPixelCountIsRejected()
{
    const std::vector<uint8_t> reference = {0, 0, 0, 255};
    const std::vector<uint8_t> actual = {0, 0, 0};

    const Chimera::ImageComparisonResult result =
        Chimera::CompareRgba8(reference, actual, 1, 1);

    Require(!result.success, "invalid pixel count must be rejected");
    Require(!result.error.empty(),
            "rejected comparison must explain the error");
}

void TestPngFilesAreDecodedAndCompared()
{
    TemporaryPngDirectory directory;
    const std::filesystem::path referencePath = directory.path / "reference.png";
    const std::filesystem::path actualPath = directory.path / "actual.png";
    const std::vector<uint8_t> reference = {10, 20, 30, 255};
    const std::vector<uint8_t> actual = {10, 23, 30, 255};
    WritePng(referencePath, 1, 1, reference);
    WritePng(actualPath, 1, 1, actual);

    const Chimera::ImageComparisonResult result = Chimera::ComparePngFiles(
        referencePath.string(), actualPath.string(), 2);

    Require(result.success, "valid PNG files should compare successfully");
    Require(result.differentPixelCount == 1,
            "PNG comparison must preserve the channel threshold");
    Require(result.maxChannelDifference == 3,
            "PNG comparison returned the wrong maximum difference");
    RequireNear(result.rmse, 1.5, "PNG comparison returned the wrong RMSE");
}

void TestPngDimensionMismatchIsRejected()
{
    TemporaryPngDirectory directory;
    const std::filesystem::path referencePath = directory.path / "reference.png";
    const std::filesystem::path actualPath = directory.path / "actual.png";
    WritePng(referencePath, 1, 1, {0, 0, 0, 255});
    WritePng(actualPath, 2, 1, {0, 0, 0, 255, 0, 0, 0, 255});

    const Chimera::ImageComparisonResult result = Chimera::ComparePngFiles(
        referencePath.string(), actualPath.string());

    Require(!result.success, "different PNG dimensions must be rejected");
    Require(!result.error.empty(),
            "dimension mismatch must provide an error message");
}

void TestMissingPngIsRejected()
{
    TemporaryPngDirectory directory;
    const std::filesystem::path referencePath = directory.path / "reference.png";
    WritePng(referencePath, 1, 1, {0, 0, 0, 255});

    const Chimera::ImageComparisonResult result = Chimera::ComparePngFiles(
        referencePath.string(), (directory.path / "missing.png").string());

    Require(!result.success, "a missing PNG must be rejected");
    Require(result.error.find("actual") != std::string::npos,
            "load error must identify the failed image role");
}

void TestDifferencePngVisualizesAmplifiedError()
{
    TemporaryPngDirectory directory;
    const std::filesystem::path referencePath = directory.path / "reference.png";
    const std::filesystem::path actualPath = directory.path / "actual.png";
    const std::filesystem::path differencePath = directory.path / "difference.png";
    WritePng(referencePath, 1, 1, {10, 20, 30, 255});
    WritePng(actualPath, 1, 1, {12, 25, 20, 255});

    const Chimera::ImageComparisonResult result =
        Chimera::ComparePngFilesAndWriteDifference(
            referencePath.string(), actualPath.string(),
            differencePath.string(), 2, 10);

    Require(result.success, "difference PNG should be written successfully");
    Require(std::filesystem::exists(differencePath),
            "difference PNG was not created");

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(differencePath.string().c_str(), &width,
                                &height, &channels, STBI_rgb);
    Require(pixels != nullptr, "difference PNG could not be decoded");
    const std::vector<uint8_t> decoded(pixels, pixels + 3);
    stbi_image_free(pixels);

    Require(width == 1 && height == 1,
            "difference PNG dimensions are incorrect");
    Require(decoded == std::vector<uint8_t>({0, 50, 100}),
            "difference PNG does not contain amplified RGB error");
}

void TestDifferencePngReportsWriteFailure()
{
    TemporaryPngDirectory directory;
    const std::filesystem::path referencePath = directory.path / "reference.png";
    const std::filesystem::path actualPath = directory.path / "actual.png";
    WritePng(referencePath, 1, 1, {0, 0, 0, 255});
    WritePng(actualPath, 1, 1, {1, 0, 0, 255});

    const Chimera::ImageComparisonResult result =
        Chimera::ComparePngFilesAndWriteDifference(
            referencePath.string(), actualPath.string(),
            (directory.path / "missing-directory" / "difference.png")
                .string());

    Require(!result.success, "difference PNG write failure must be reported");
    Require(result.error.find("write") != std::string::npos,
            "difference PNG write failure must explain the error");
}

void TestPassingRegressionDoesNotWriteDifferencePng()
{
    TemporaryPngDirectory directory;
    const std::filesystem::path baselinePath = directory.path / "baseline.png";
    const std::filesystem::path actualPath = directory.path / "actual.png";
    const std::filesystem::path differencePath = directory.path / "difference.png";
    WritePng(baselinePath, 1, 1, {10, 20, 30, 255});
    WritePng(actualPath, 1, 1, {11, 20, 30, 255});

    Chimera::ImageRegressionSettings settings;
    settings.channelThreshold = 1;
    const Chimera::ImageRegressionResult result = Chimera::RunImageRegression(
        baselinePath.string(), actualPath.string(), differencePath.string(),
        settings);

    Require(result.success, "passing regression should execute successfully");
    Require(result.passed, "difference within threshold should pass regression");
    Require(!std::filesystem::exists(differencePath),
            "passing regression should not create a difference PNG");
}

void TestFailingRegressionWritesDifferencePng()
{
    TemporaryPngDirectory directory;
    const std::filesystem::path baselinePath = directory.path / "baseline.png";
    const std::filesystem::path actualPath = directory.path / "actual.png";
    const std::filesystem::path differencePath = directory.path / "difference.png";
    WritePng(baselinePath, 1, 1, {10, 20, 30, 255});
    WritePng(actualPath, 1, 1, {20, 20, 30, 255});

    Chimera::ImageRegressionSettings settings;
    settings.channelThreshold = 2;
    settings.allowedDifferentPixelCount = 0;
    settings.allowedMaxChannelDifference = 20;
    settings.allowedRmse = 10.0;
    const Chimera::ImageRegressionResult result = Chimera::RunImageRegression(
        baselinePath.string(), actualPath.string(), differencePath.string(),
        settings);

    Require(result.success, "failing regression should still execute successfully");
    Require(!result.passed, "out-of-tolerance image should fail regression");
    Require(result.comparison.differentPixelCount == 1,
            "regression should expose comparison metrics");
    Require(result.differencePath == differencePath.string(),
            "regression should report its difference PNG");
    Require(std::filesystem::exists(differencePath),
            "failing regression should create a difference PNG");
}

void TestRegressionAppliesEveryAcceptanceLimit()
{
    TemporaryPngDirectory directory;
    const std::filesystem::path baselinePath = directory.path / "baseline.png";
    const std::filesystem::path actualPath = directory.path / "actual.png";
    WritePng(baselinePath, 1, 1, {0, 0, 0, 255});
    WritePng(actualPath, 1, 1, {10, 0, 0, 255});

    Chimera::ImageRegressionSettings settings;
    settings.allowedDifferentPixelCount = 1;
    settings.allowedMaxChannelDifference = 9;
    settings.allowedRmse = 10.0;
    Chimera::ImageRegressionResult result = Chimera::RunImageRegression(
        baselinePath.string(), actualPath.string(),
        (directory.path / "max-difference.png").string(), settings);
    Require(result.success && !result.passed,
            "maximum channel limit must independently reject regression");

    settings.allowedMaxChannelDifference = 10;
    settings.allowedRmse = 4.9;
    result = Chimera::RunImageRegression(
        baselinePath.string(), actualPath.string(),
        (directory.path / "rmse-difference.png").string(), settings);
    Require(result.success && !result.passed,
            "RMSE limit must independently reject regression");
}

void TestRegressionSignatureRoundTripAndMismatch()
{
    TemporaryPngDirectory directory;
    const std::filesystem::path signaturePath = directory.path / "baseline.txt";
    const std::string signature =
        "version=1\nrenderPath=Forward\nwidth=1600\nheight=900\n";
    std::string error;
    Require(Chimera::WriteImageRegressionSignature(
                signaturePath.string(), signature, error),
            "regression signature should be written");

    Chimera::ImageRegressionSignatureResult result =
        Chimera::ValidateImageRegressionSignature(signaturePath.string(),
                                                  signature);
    Require(result.success && result.matches,
            "stored regression signature should match its source");

    result = Chimera::ValidateImageRegressionSignature(
        signaturePath.string(), signature + "renderFlags=1\n");
    Require(result.success && !result.matches,
            "changed render configuration must not match baseline signature");
    Require(!result.error.empty(),
            "signature mismatch should explain why comparison is blocked");
}

void TestMissingRegressionSignatureIsRejected()
{
    TemporaryPngDirectory directory;
    const Chimera::ImageRegressionSignatureResult result =
        Chimera::ValidateImageRegressionSignature(
            (directory.path / "missing.txt").string(), "version=1\n");
    Require(!result.success && !result.matches,
            "missing regression signature must be rejected");
}

void TestWarmupCompletesAfterExactRenderedFrameCount()
{
    Chimera::FrameWarmupCounter warmup;
    warmup.Start(3);

    Require(warmup.IsActive() && warmup.GetRemainingFrames() == 3,
            "warm-up should start with the requested frame count");
    Require(!warmup.AdvanceAfterRenderedFrame() &&
                warmup.GetRemainingFrames() == 2,
            "warm-up completed one frame too early");
    Require(!warmup.AdvanceAfterRenderedFrame() &&
                warmup.GetRemainingFrames() == 1,
            "warm-up completed one frame too early");
    Require(warmup.AdvanceAfterRenderedFrame(),
            "warm-up did not complete on the requested rendered frame");
    Require(!warmup.IsActive() && warmup.GetRemainingFrames() == 0,
            "completed warm-up should become inactive");
    Require(!warmup.AdvanceAfterRenderedFrame(),
            "completed warm-up must not trigger more than once");
}

void TestTemporalHistoryResetClearsJitter()
{
    Chimera::EditorCamera camera(45.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    camera.SetViewportSize(1600.0f, 900.0f);
    camera.UpdateTAAState(5, true);
    Require(camera.GetJitter() != glm::vec2(0.0f),
            "enabled TAA should produce sub-pixel jitter");

    camera.ResetTemporalHistory();
    Require(camera.GetJitter() == glm::vec2(0.0f) &&
                camera.GetPrevJitter() == glm::vec2(0.0f),
            "temporal reset should clear current and previous jitter");
}

void TestCaptureReadinessRequiresStableRenderedFrames()
{
    Chimera::CaptureReadinessTracker readiness(2);
    readiness.Update(true, "scene-a");
    Require(!readiness.IsReadyFor("scene-a") &&
                readiness.GetStableFrameCount() == 1,
            "capture became ready before the required stable frame count");

    readiness.Update(true, "scene-a");
    Require(readiness.IsReadyFor("scene-a") &&
                readiness.GetStableFrameCount() == 2,
            "capture did not become ready after stable rendered frames");

    readiness.Update(true, "scene-b");
    Require(!readiness.IsReadyFor("scene-b") &&
                readiness.GetStableFrameCount() == 1,
            "scene signature change did not restart stabilization");

    readiness.Update(false, "scene-b");
    Require(!readiness.IsReadyFor("scene-b") &&
                readiness.GetStableFrameCount() == 0,
            "unready GPU prerequisites did not reset stabilization");
}
} // namespace

int main()
{
    try
    {
        TestIdenticalPixelsProduceNoDifference();
        std::cout << "[PASS] identical pixels produce no difference\n";

        TestDifferenceMetricsAreCalculated();
        std::cout << "[PASS] image difference metrics are calculated\n";

        TestThresholdOnlyAffectsPixelCount();
        std::cout << "[PASS] threshold only affects pixel count\n";

        TestInvalidPixelCountIsRejected();
        std::cout << "[PASS] invalid pixel count is rejected\n";

        TestPngFilesAreDecodedAndCompared();
        std::cout << "[PASS] PNG files are decoded and compared\n";

        TestPngDimensionMismatchIsRejected();
        std::cout << "[PASS] PNG dimension mismatch is rejected\n";

        TestMissingPngIsRejected();
        std::cout << "[PASS] missing PNG is rejected\n";

        TestDifferencePngVisualizesAmplifiedError();
        std::cout << "[PASS] difference PNG visualizes amplified error\n";

        TestDifferencePngReportsWriteFailure();
        std::cout << "[PASS] difference PNG write failure is reported\n";

        TestPassingRegressionDoesNotWriteDifferencePng();
        std::cout << "[PASS] passing regression skips difference PNG\n";

        TestFailingRegressionWritesDifferencePng();
        std::cout << "[PASS] failing regression writes difference PNG\n";

        TestRegressionAppliesEveryAcceptanceLimit();
        std::cout << "[PASS] regression applies every acceptance limit\n";

        TestRegressionSignatureRoundTripAndMismatch();
        std::cout << "[PASS] regression signature round-trip and mismatch\n";

        TestMissingRegressionSignatureIsRejected();
        std::cout << "[PASS] missing regression signature is rejected\n";

        TestWarmupCompletesAfterExactRenderedFrameCount();
        std::cout << "[PASS] warm-up completes after exact frame count\n";

        TestTemporalHistoryResetClearsJitter();
        std::cout << "[PASS] temporal history reset clears jitter\n";

        TestCaptureReadinessRequiresStableRenderedFrames();
        std::cout << "[PASS] capture readiness requires stable frames\n";

        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
