#ifndef SHADER_COMMON_H
#define SHADER_COMMON_H

#ifdef __cplusplus
#include <glm/glm.hpp>
#include <cstdint>
namespace Chimera {
using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;
using uvec4 = glm::uvec4;
using mat4 = glm::mat4;
using uint = uint32_t;
using uint64 = uint64_t;
#else
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_scalar_block_layout : enable

// Define uint64 for GLSL
#define uint64 uint64_t
#endif

// --- 0. Shared Bindings ---
#define BINDING_GLOBAL_UBO          0
#define BINDING_AS                  0
#define BINDING_MATERIALS           1
#define BINDING_PRIMITIVES           2
#define BINDING_TEXTURES            3
#define BINDING_RT_OUTPUT           0
#define BINDING_RT_MOTION           1

// --- 1. Shared Constants ---
#define REVERSED_Z              1 // 1: Near=1.0, Far=0.0 (High Precision)

#define DISPLAY_MODE_FINAL      0
#define DISPLAY_MODE_ALBEDO     1
#define DISPLAY_MODE_NORMAL     2
#define DISPLAY_MODE_MATERIAL   3
#define DISPLAY_MODE_MOTION     4
#define DISPLAY_MODE_DEPTH      5
#define DISPLAY_MODE_SHADOW_AO  6
#define DISPLAY_MODE_REFLECTION 7
#define DISPLAY_MODE_GI         8
#define DISPLAY_MODE_EMISSIVE   9

#define RENDER_FLAG_SVGF_BIT        (1 << 0)
#define RENDER_FLAG_GI_BIT          (1 << 1)
#define RENDER_FLAG_SHOW_VARIANCE   (1 << 2)
#define RENDER_FLAG_SHADOW_BIT      (1 << 3)
#define RENDER_FLAG_REFLECTION_BIT  (1 << 4)
#define RENDER_FLAG_TAA_BIT         (1 << 5)
#define RENDER_FLAG_TAA_HISTORY_BIT (1 << 6) // [NEW] Bit to indicate if history is available
#define RENDER_FLAG_LIGHT_BIT       (1 << 7) // [NEW] Control for the main sun light

// --- 2. Data Structures ---

struct GpuMaterial
{
    vec4 albedo;
    vec4 emission;
    float roughness;
    float metallic;
    
    int albedoTex;
    int normalTex;
    int metalRoughTex;
    int emissiveTex; 
    int aoTex;       
    
    int padding[5]; // Total: 16+16+4+4+4*5 + 4*5 = 80 bytes (16-byte aligned)
};

struct GpuPrimitive
{
    mat4 transform;
    mat4 normalMatrix;
    mat4 prevTransform;
    uint64 vertexAddress; 
    uint64 indexAddress;  
    int materialIndex;
    int padding[3];
};

struct GpuVertex
{
    vec3 pos;
    vec3 normal;
    vec4 tangent;
    vec2 texCoord;
};

// --- 3. UBO & Ray Tracing Payload ---

struct CameraData 
{
    mat4 view;
    mat4 proj;
    mat4 viewInverse;
    mat4 projInverse;
    mat4 viewProjInverse;
    mat4 prevView;
    mat4 prevProj;
    vec4 position;
    vec4 jitterData;
};

struct LightData 
{
    mat4 projview;
    vec4 direction;
    vec4 color;
    vec4 intensity; 
};

struct UniformBufferObject
{
    CameraData camera;
    LightData  sunLight;

    // Block 1: Sizes & Frame Info
    vec4 displayData; // x: width, y: height, z: 1/width, w: 1/height

    // Block 2: Indices & Flags
    uvec4 frameData; // x: frameIndex, y: frameCount, z: displayMode, w: renderFlags

    // Block 3: Lighting & Post Parameters
    vec4 postData;   // x: exposure, y: ambientStrength, zw: blueNoiseTextureIndex and padding

    // Block 4: Environment & Other
    vec4 envData;    // x: skyboxTextureIndex, yzw: padding

    vec4 svgfAlpha;  // x: alphaColor, y: alphaMoments, zw: padding
    vec4 svgfPhi;    // x: phiColor, y: phiNormal, z: phiDepth, w: padding
    vec4 gpuClearColor; 
};

#ifndef __cplusplus
layout(set = 0, binding = BINDING_GLOBAL_UBO) uniform GlobalUBO 
{
    CameraData camera;
    LightData  sunLight;

    // Block 1: Sizes & Frame Info
    vec4 displayData; // x: width, y: height, z: 1/width, w: 1/height

    // Block 2: Indices & Flags
    uvec4 frameData; // x: frameIndex, y: frameCount, z: displayMode, w: renderFlags

    // Block 3: Lighting & Post Parameters
    vec4 postData;   // x: exposure, y: ambientStrength, zw: blueNoiseTextureIndex and padding

    // Block 4: Environment & Other
    vec4 envData;    // x: skyboxTextureIndex, yzw: padding

    vec4 svgfAlpha;  // x: alphaColor, y: alphaMoments, zw: padding
    vec4 svgfPhi;    // x: phiColor, y: phiNormal, z: phiDepth, w: padding
    vec4 gpuClearColor; 
};
#endif

struct HitPayload
{
    vec4 color_dist;   // rgb: color, a: distance
    vec4 normal_rough; // rgb: normal, a: roughness
    vec4 motion_hit;   // rg: motion, b: hit (bool as float), a: padding
};

struct ScenePushConstants
{
    uint objectId;
};

#ifdef __cplusplus
static_assert(sizeof(GpuPrimitive) == 224, "GpuPrimitive size mismatch");
static_assert(sizeof(GpuMaterial) == 80, "GpuMaterial size mismatch");
static_assert(sizeof(UniformBufferObject) % 16 == 0, "UBO alignment mismatch");
} // namespace Chimera
#endif

#endif
