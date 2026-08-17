#include "Renderer/Backend/ShaderCommon.h"

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <type_traits>

using namespace Chimera;

static_assert(std::is_standard_layout_v<CameraData>);
static_assert(std::is_standard_layout_v<LightData>);
static_assert(std::is_standard_layout_v<UniformBufferObject>);
static_assert(std::is_standard_layout_v<GpuMaterial>);

static_assert(sizeof(CameraData) == 480);
static_assert(sizeof(LightData) == 112);

static_assert(offsetof(UniformBufferObject, camera) == 0);
static_assert(offsetof(UniformBufferObject, sunLight) == 480);
static_assert(offsetof(UniformBufferObject, displayData) == 592);
static_assert(offsetof(UniformBufferObject, frameData) == 608);
static_assert(offsetof(UniformBufferObject, postData) == 624);
static_assert(offsetof(UniformBufferObject, envData) == 640);
static_assert(offsetof(UniformBufferObject, svgfAlpha) == 656);
static_assert(offsetof(UniformBufferObject, svgfPhi) == 672);
static_assert(offsetof(UniformBufferObject, gpuClearColor) == 688);
static_assert(sizeof(UniformBufferObject) == 704);

static_assert(offsetof(GpuMaterial, roughness) == 12);
static_assert(offsetof(GpuMaterial, colour) == 16);
static_assert(offsetof(GpuMaterial, metallic) == 28);
static_assert(offsetof(GpuMaterial, scatteringColour) == 48);
static_assert(offsetof(GpuMaterial, transmissionDepth) == 60);
static_assert(offsetof(GpuMaterial, emissionTexture) == 64);
static_assert(offsetof(GpuMaterial, normalTexture) == 76);
static_assert(sizeof(GpuMaterial) == 80);
static_assert(static_cast<uint32_t>(DisplayMode::TAAHistory) == 12);

namespace
{
std::string ReadTextFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("could not open shader: " + path.string());
    }

    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

void TestStochasticShadersUseTemporalSampleIndex()
{
    const std::filesystem::path shaderRoot = CHIMERA_SHADER_SOURCE_DIR;
    const std::vector<std::filesystem::path> stochasticShaders = {
        "raytracing/raygen.rgen", "raytracing/rt_shadow.rgen",
        "raytracing/rt_ao.rgen", "raytracing/diffuse_gi.rgen",
        "raytracing/closesthit.rchit"};

    for (const std::filesystem::path& relativePath : stochasticShaders)
    {
        const std::string source = ReadTextFile(shaderRoot / relativePath);
        if (source.find("frameData.y") == std::string::npos)
        {
            throw std::runtime_error(
                "stochastic shader does not use temporal sample index: " +
                relativePath.string());
        }
    }

    const std::string closestHit =
        ReadTextFile(shaderRoot / "raytracing/closesthit.rchit");
    if (closestHit.find("frameData.x") != std::string::npos)
    {
        throw std::runtime_error(
            "closest-hit randomness still uses frame-in-flight index");
    }
}

void TestRaytracePrimaryRayJitterContract()
{
    // camera.jitterData.xy is expressed in NDC. Converting an NDC offset to
    // texture UV halves it, so this is the same convention used by TAA:
    // sampleUv = pixelUv - jitterNdc * 0.5.
    const glm::vec2 pixelUv(0.625f, 0.375f);
    const glm::vec2 jitterNdc(0.25f, -0.125f);
    const glm::vec2 pixelNdc = pixelUv * 2.0f - glm::vec2(1.0f);
    const glm::vec2 sampleNdc = pixelNdc - jitterNdc;
    const glm::vec2 sampleUv = sampleNdc * 0.5f + glm::vec2(0.5f);
    const glm::vec2 expectedUv = pixelUv - jitterNdc * 0.5f;

    constexpr float epsilon = 1e-6f;
    if (std::abs(sampleUv.x - expectedUv.x) > epsilon ||
        std::abs(sampleUv.y - expectedUv.y) > epsilon)
    {
        throw std::runtime_error("NDC-to-UV jitter conversion has the wrong scale or sign");
    }

    const std::filesystem::path shaderRoot = CHIMERA_SHADER_SOURCE_DIR;
    const std::string raytrace =
        ReadTextFile(shaderRoot / "raytracing/raytrace.rgen");
    if (raytrace.find("return pixelNdc - camera.jitterData.xy;") == std::string::npos ||
        raytrace.find("ApplyPrimaryRayJitter(pixelNdc)") == std::string::npos)
    {
        throw std::runtime_error(
            "ray tracing primary rays do not apply the TAA jitter convention");
    }
}

void TestRaytraceBackgroundMotionContract()
{
    const std::filesystem::path shaderRoot = CHIMERA_SHADER_SOURCE_DIR;
    const std::string raytrace =
        ReadTextFile(shaderRoot / "raytracing/raytrace.rgen");

    if (raytrace.find("camera.prevView * vec4(worldDirection, 0.0)") == std::string::npos)
    {
        throw std::runtime_error(
            "ray tracing background motion does not ignore camera translation");
    }
    if (raytrace.find("return currentUv - prevUv;") == std::string::npos ||
        raytrace.find("CalculateBackgroundMotion(direction, sampleUv)") == std::string::npos)
    {
        throw std::runtime_error(
            "ray tracing misses do not use previous-frame directional reprojection");
    }
}

void TestRaytraceMotionDebugViewContract()
{
    const std::filesystem::path shaderRoot = CHIMERA_SHADER_SOURCE_DIR;
    const std::string raytrace =
        ReadTextFile(shaderRoot / "raytracing/raytrace.rgen");

    if (raytrace.find("frameData.z == DISPLAY_MODE_MOTION") == std::string::npos ||
        raytrace.find("abs(motionVector) * 10.0") == std::string::npos)
    {
        throw std::runtime_error(
            "ray tracing Motion display mode does not visualize its motion image values");
    }
}

void TestTaaNeighborhoodClampsTexelCoordinates()
{
    const std::filesystem::path shaderRoot = CHIMERA_SHADER_SOURCE_DIR;
    const std::string taa =
        ReadTextFile(shaderRoot / "postprocess/taa.comp");

    if (taa.find("ClampPixel(pixel + ivec2(x, y), depthExtent)") == std::string::npos ||
        taa.find("texelFetch(gMotion, closestPixel, 0)") == std::string::npos ||
        taa.find("ClampPixel(pixel + ivec2(x, y), colorExtent)") == std::string::npos)
    {
        throw std::runtime_error(
            "TAA neighborhood texelFetch coordinates are not clamped at image edges");
    }
}

void TestTaaRejectsInconsistentDepthHistory()
{
    const std::filesystem::path shaderRoot = CHIMERA_SHADER_SOURCE_DIR;
    const std::string taa =
        ReadTextFile(shaderRoot / "postprocess/taa.comp");

    if (taa.find("uniform sampler2D historyDepth") == std::string::npos ||
        taa.find("camera.prevProj * camera.prevView") == std::string::npos ||
        taa.find("IsDepthHistoryConsistent(unjitteredUV, prevUV, currentDepth)") ==
            std::string::npos ||
        taa.find("if (!historyAccepted) alpha = 1.0;") == std::string::npos)
    {
        throw std::runtime_error(
            "TAA does not reject history from a different depth surface");
    }
}

void TestTaaHistoryDebugViewContract()
{
    const std::filesystem::path shaderRoot = CHIMERA_SHADER_SOURCE_DIR;
    const std::string taa =
        ReadTextFile(shaderRoot / "postprocess/taa.comp");

    if (taa.find("frameData.z == DISPLAY_MODE_TAA_HISTORY") == std::string::npos ||
        taa.find("historyAvailable && depthHistoryConsistent") == std::string::npos ||
        taa.find("historyAccepted ? vec3(0.0, 1.0, 0.0)") == std::string::npos)
    {
        throw std::runtime_error(
            "TAA History display mode does not visualize history acceptance");
    }
}
} // namespace

int main()
{
    try
    {
        std::cout << "[PASS] shader ABI layout matches expected offsets\n";
        TestStochasticShadersUseTemporalSampleIndex();
        std::cout << "[PASS] stochastic shaders use temporal sample index\n";
        TestRaytracePrimaryRayJitterContract();
        std::cout << "[PASS] ray tracing primary rays share the TAA jitter convention\n";
        TestRaytraceBackgroundMotionContract();
        std::cout << "[PASS] ray tracing background motion uses directional reprojection\n";
        TestRaytraceMotionDebugViewContract();
        std::cout << "[PASS] ray tracing Motion display mode visualizes motion vectors\n";
        TestTaaNeighborhoodClampsTexelCoordinates();
        std::cout << "[PASS] TAA neighborhood fetches clamp image-edge coordinates\n";
        TestTaaRejectsInconsistentDepthHistory();
        std::cout << "[PASS] TAA rejects history with inconsistent depth\n";
        TestTaaHistoryDebugViewContract();
        std::cout << "[PASS] TAA History display mode visualizes history acceptance\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
