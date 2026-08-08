#include "Renderer/Backend/ShaderCommon.h"

#include <cstddef>
#include <iostream>
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

int main()
{
    std::cout << "[PASS] shader ABI layout matches expected offsets\n";
    return 0;
}