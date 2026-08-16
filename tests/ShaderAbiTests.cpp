#include "Renderer/Backend/ShaderCommon.h"

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
} // namespace

int main()
{
    try
    {
        std::cout << "[PASS] shader ABI layout matches expected offsets\n";
        TestStochasticShadersUseTemporalSampleIndex();
        std::cout << "[PASS] stochastic shaders use temporal sample index\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
