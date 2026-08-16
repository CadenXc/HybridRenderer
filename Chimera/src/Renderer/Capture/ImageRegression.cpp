#include "pch.h"
#include "ImageRegression.h"

#include <fstream>
#include <sstream>

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

bool WriteImageRegressionSignature(
    const std::string& signaturePath, const std::string& signature,
    std::string& error)
{
    if (signaturePath.empty())
    {
        error = "signature path must not be empty";
        return false;
    }

    std::ofstream output(signaturePath, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        error = "failed to open regression signature for writing: " +
                signaturePath;
        return false;
    }

    output.write(signature.data(), static_cast<std::streamsize>(signature.size()));
    if (!output)
    {
        error = "failed to write regression signature: " + signaturePath;
        return false;
    }

    error.clear();
    return true;
}

ImageRegressionSignatureResult ValidateImageRegressionSignature(
    const std::string& signaturePath, const std::string& expectedSignature)
{
    ImageRegressionSignatureResult result;
    std::ifstream input(signaturePath, std::ios::binary);
    if (!input)
    {
        result.error = "failed to open regression signature: " +
                       signaturePath;
        return result;
    }

    std::ostringstream contents;
    contents << input.rdbuf();
    if (input.bad())
    {
        result.error = "failed to read regression signature: " +
                       signaturePath;
        return result;
    }

    result.success = true;
    result.matches = contents.str() == expectedSignature;
    if (!result.matches)
    {
        result.error =
            "current render configuration does not match the baseline";
    }
    return result;
}
} // namespace Chimera
