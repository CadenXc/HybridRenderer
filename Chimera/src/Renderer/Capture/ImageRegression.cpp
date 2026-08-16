#include "pch.h"
#include "ImageRegression.h"

namespace Chimera
{
ImageRegressionResult RunImageRegression(
    const std::string& baselinePath, const std::string& actualPath,
    const std::string& differencePath,
    const ImageRegressionSettings& settings)
{
    ImageRegressionResult result;
    result.comparison = ComparePngFiles(
        baselinePath, actualPath, settings.channelThreshold);

    if (!result.comparison.success)
    {
        result.error = result.comparison.error;
        return result;
    }

    result.passed =
        result.comparison.differentPixelCount <=
            settings.allowedDifferentPixelCount &&
        result.comparison.maxChannelDifference <=
            settings.allowedMaxChannelDifference &&
        result.comparison.rmse <= settings.allowedRmse;

    if (!result.passed)
    {
        if (differencePath.empty())
        {
            result.error = "difference output path must not be empty";
            return result;
        }

        const ImageComparisonResult differenceResult =
            ComparePngFilesAndWriteDifference(
                baselinePath, actualPath, differencePath,
                settings.channelThreshold,
                settings.differenceAmplification);
        if (!differenceResult.success)
        {
            result.error = differenceResult.error;
            return result;
        }

        result.differencePath = differencePath;
    }

    result.success = true;
    return result;
}
} // namespace Chimera
