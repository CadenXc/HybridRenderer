#ifndef SHADER_COMMON_H
#define SHADER_COMMON_H

#ifdef __cplusplus
#include <glm/glm.hpp>
#include <cstdint>
namespace Chimera {
using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;
using mat4 = glm::mat4;
using uint = uint32_t;
#else
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_scalar_block_layout : enable

// --- GLSL Helper Functions ---
vec3 GetWorldPos(float depth, vec2 uv, mat4 invViewProj)
{
    vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 world = invViewProj * clip;
    return world.xyz / world.w;
}

vec3 GetViewPos(float depth, vec2 uv, mat4 invProj)
{
    vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 view = invProj * clip;
    return view.xyz / view.w;
}
#endif

// --- 1. Shared Constants (Anti-Magic-Number) ---
#define DISPLAY_MODE_FINAL      0
#define DISPLAY_MODE_ALBEDO     1
#define DISPLAY_MODE_NORMAL     2
#define DISPLAY_MODE_MATERIAL   3
#define DISPLAY_MODE_MOTION     4
#define DISPLAY_MODE_DEPTH      5
#define DISPLAY_MODE_SHADOW_AO  6
#define DISPLAY_MODE_REFLECTION 7

#define RENDER_FLAG_SVGF_BIT        (1 << 0)
#define RENDER_FLAG_GI_BIT          (1 << 1)
#define RENDER_FLAG_SHOW_VARIANCE   (1 << 2)

// --- 2. Logical Data Groups ---
struct CameraData 
{
    mat4 view;
    mat4 proj;
    mat4 viewInverse;
    mat4 projInverse;
    mat4 viewProjInverse;
    mat4 prevView;
    mat4 prevProj;
    vec4 position; // w = 1.0
};

struct LightData 
{
    mat4 projview;
    vec4 direction;
    vec4 color;
    vec4 intensity; // x: intensity, y: radius, zw: unused
};

struct SVGFParams 
{
    float alphaColor;
    float alphaMoments;
    float phiColor;
    float phiNormal;
    float phiDepth;
    float padding[3]; 
};

// --- 3. Main Structures ---
struct UniformBufferObject 
{
    CameraData camera;
    LightData  sunLight;
    SVGFParams svgf;

    vec2 displaySize;
    vec2 displaySizeInverse;
    uint frameIndex;
    uint frameCount;

    uint displayMode; 
    uint renderFlags;
    float exposure;
    float ambientStrength;

    float bloomStrength;
    float padding_final;
};

struct GpuMaterial
{
    vec4 albedo;
    vec4 emission;
    float roughness;
    float metallic;
    int albedoTex;
    int normalTex;
    int metalRoughTex;
    int padding[3];
};

struct GpuVertex
{
    vec3 pos;
    vec3 normal;
    vec4 tangent;
    vec2 texCoord;
};

// --- 4. Special Purpose ---
struct GBufferPushConstants
{
    mat4 model;
    mat4 normalMatrix;
    mat4 prevModel;
    int materialIndex;
};

struct RTInstanceData
{
    uint64_t vertexAddress;
    uint64_t indexAddress;
    int materialIndex;
    int padding;
};

struct HitPayload
{
    vec3 color;
    float distance;
    vec3 normal;
    float roughness;
    bool hit;
};

#ifdef __cplusplus
static_assert(sizeof(GpuMaterial) == 64, "GpuMaterial size must be 64 bytes");
static_assert(sizeof(GpuVertex) == 48, "GpuVertex size must be 48 bytes");
static_assert(sizeof(UniformBufferObject) % 16 == 0, "UBO size must be 16-byte aligned");
} // namespace Chimera
#endif

#endif
