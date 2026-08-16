#include "Renderer/Capture/ImageComparison.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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

        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
