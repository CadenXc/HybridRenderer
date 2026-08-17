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
static_assert(RenderFlags_TAAHighQualityBit == (1u << 12));
static_assert(RenderFlags_ManualOutputSrgbBit == (1u << 13));

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

void TestTaaUsesCatmullRomHistoryReconstruction()
{
    const std::filesystem::path shaderRoot = CHIMERA_SHADER_SOURCE_DIR;
    const std::string taa =
        ReadTextFile(shaderRoot / "postprocess/taa.comp");

    if (taa.find("vec3 SampleHistoryCatmullRom(vec2 uv)") == std::string::npos ||
        taa.find("RENDER_FLAG_TAA_HIGH_QUALITY_BIT") == std::string::npos ||
        taa.find("? SampleHistoryCatmullRom(prevUV)") == std::string::npos ||
        taa.find(": texture(historyColor, prevUV).rgb") == std::string::npos)
    {
        throw std::runtime_error(
            "TAA history quality flag does not select Catmull-Rom or bilinear reconstruction");
    }
}

void TestPostprocessHasNonSrgbSwapchainFallback()
{
    const std::filesystem::path shaderRoot = CHIMERA_SHADER_SOURCE_DIR;
    const std::string postprocess =
        ReadTextFile(shaderRoot / "postprocess/postprocess.frag");

    const size_t toneMap = postprocess.find("color = ToneMapACES(color);");
    const size_t fallbackFlag =
        postprocess.find("RENDER_FLAG_MANUAL_OUTPUT_SRGB_BIT", toneMap);
    const size_t srgbEncode =
        postprocess.find("color = LinearToSrgb(color);", fallbackFlag);
    if (toneMap == std::string::npos || fallbackFlag == std::string::npos ||
        srgbEncode == std::string::npos)
    {
        throw std::runtime_error(
            "postprocess does not encode tone-mapped output for non-sRGB swapchains");
    }
}

void TestNormalMapTangentFrameIsOrthogonalized()
{
    const glm::vec3 normal = glm::normalize(glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 interpolatedTangent(1.0f, 0.0f, 0.5f);
    const glm::vec3 tangent = glm::normalize(
        interpolatedTangent -
        normal * glm::dot(normal, interpolatedTangent));
    if (std::abs(glm::dot(normal, tangent)) > 1e-6f)
    {
        throw std::runtime_error(
            "Gram-Schmidt tangent must be perpendicular to the surface normal");
    }

    const std::filesystem::path shaderRoot = CHIMERA_SHADER_SOURCE_DIR;
    const std::string common =
        ReadTextFile(shaderRoot / "common/common.glsl");
    if (common.find("tangent.xyz - surfaceNormal * dot(surfaceNormal, tangent.xyz)") ==
            std::string::npos ||
        common.find("cross(surfaceNormal, T) * handedness") ==
            std::string::npos)
    {
        throw std::runtime_error(
            "normal mapping does not construct an orthogonal handed TBN frame");
    }
}

void TestEnvironmentConsumersHonorIblFlag()
{
    const std::filesystem::path shaderRoot = CHIMERA_SHADER_SOURCE_DIR;
    const std::vector<std::filesystem::path> environmentConsumers = {
        "forward/forward.frag",
        "postprocess/composition.frag",
        "postprocess/skybox.frag",
        "raytracing/closesthit.rchit",
        "raytracing/miss.rmiss",
        "raytracing/rayquery.frag",
        "raytracing/raytrace.rgen"};

    for (const std::filesystem::path& relativePath : environmentConsumers)
    {
        const std::string source = ReadTextFile(shaderRoot / relativePath);
        if (source.find("RENDER_FLAG_IBL_BIT") == std::string::npos)
        {
            throw std::runtime_error(
                "environment consumer ignores the IBL render flag: " +
                relativePath.string());
        }
    }
}

void TestDirectLightShadersHonorLightAndShadowFlags()
{
    const std::filesystem::path shaderRoot = CHIMERA_SHADER_SOURCE_DIR;
    const std::vector<std::filesystem::path> directLightShaders = {
        "forward/forward.frag",
        "raytracing/closesthit.rchit",
        "raytracing/raygen.rgen",
        "raytracing/rayquery.frag",
        "raytracing/rt_shadow.rgen"};

    for (const std::filesystem::path& relativePath : directLightShaders)
    {
        const std::string source = ReadTextFile(shaderRoot / relativePath);
        if (source.find("RENDER_FLAG_LIGHT_BIT") == std::string::npos ||
            source.find("RENDER_FLAG_SHADOW_BIT") == std::string::npos ||
            source.find("lightEnabled && shadowsEnabled") == std::string::npos)
        {
            throw std::runtime_error(
                "direct-light shader does not separate radiance and visibility flags: " +
                relativePath.string());
        }
    }
}

void TestHybridShadowVisibilityMatchesSunDirection()
{
    const std::filesystem::path shaderRoot = CHIMERA_SHADER_SOURCE_DIR;
    const std::string shadow =
        ReadTextFile(shaderRoot / "raytracing/rt_shadow.rgen");
    const std::string composition =
        ReadTextFile(shaderRoot / "postprocess/composition.frag");

    if (shadow.find("SampleLights(") != std::string::npos ||
        shadow.find("SampleConeAroundDirection(") == std::string::npos ||
        shadow.find("-sunLight.direction.xyz") == std::string::npos ||
        composition.find("normalize(-sunLight.direction.xyz)") ==
            std::string::npos ||
        composition.find("shadowFactor * lightIntensity") ==
            std::string::npos)
    {
        throw std::runtime_error(
            "Hybrid shadow visibility is not evaluated for the composed sun light");
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
        TestTaaUsesCatmullRomHistoryReconstruction();
        std::cout << "[PASS] TAA history uses Catmull-Rom reconstruction\n";
        TestPostprocessHasNonSrgbSwapchainFallback();
        std::cout << "[PASS] non-sRGB swapchains use manual output encoding\n";
        TestNormalMapTangentFrameIsOrthogonalized();
        std::cout << "[PASS] normal mapping uses an orthogonal handed TBN frame\n";
        TestEnvironmentConsumersHonorIblFlag();
        std::cout << "[PASS] all environment consumers honor the IBL flag\n";
        TestDirectLightShadersHonorLightAndShadowFlags();
        std::cout << "[PASS] direct-light shaders separate light and shadow flags\n";
        TestHybridShadowVisibilityMatchesSunDirection();
        std::cout << "[PASS] Hybrid shadow visibility matches the composed sun direction\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
