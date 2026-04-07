#ifndef SHADER_COMMON_H
#define SHADER_COMMON_H

#ifdef __cplusplus
#include <glm/glm.hpp>
#include <cstdint>
namespace Chimera
{
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
#define BINDING_GLOBAL_UBO            0
#define BINDING_AS                    0
#define BINDING_MATERIALS             1
#define BINDING_INSTANCES             2
#define BINDING_TEXTURES              3
#define BINDING_RT_OUTPUT             0
#define BINDING_RT_MOTION             1

// --- 1. Shared Constants ---
#define INVALID_ID -1

#define MATERIAL_TYPE_MATTE 0
#define MATERIAL_TYPE_PBR   1
#define MATERIAL_TYPE_VOLUMETRIC   2
#define MATERIAL_TYPE_GLASS   3
#define MATERIAL_TYPE_SUBSURFACE   4

#define DISPLAY_MODE_FINAL            0
#define DISPLAY_MODE_ALBEDO           1
#define DISPLAY_MODE_NORMAL           2
#define DISPLAY_MODE_MATERIAL         3
#define DISPLAY_MODE_MOTION           4
#define DISPLAY_MODE_DEPTH            5
#define DISPLAY_MODE_SHADOW           6
#define DISPLAY_MODE_AO               7
#define DISPLAY_MODE_REFLECTION       8
#define DISPLAY_MODE_GI               9
#define DISPLAY_MODE_EMISSIVE         10
#define DISPLAY_MODE_SVGF_VARIANCE    11

#define RENDER_FLAG_LIGHT_BIT         (1 << 0)
#define RENDER_FLAG_SHADOW_BIT        (1 << 1)
#define RENDER_FLAG_AO_BIT            (1 << 2)
#define RENDER_FLAG_REFLECTION_BIT    (1 << 3)
#define RENDER_FLAG_GI_BIT            (1 << 4)
#define RENDER_FLAG_TAA_BIT           (1 << 5)
#define RENDER_FLAG_TAA_HISTORY_BIT   (1 << 6)
#define RENDER_FLAG_SVGF_BIT          (1 << 7)
#define RENDER_FLAG_SVGF_TEMPORAL_BIT (1 << 8)
#define RENDER_FLAG_SVGF_SPATIAL_BIT  (1 << 9)
#define RENDER_FLAG_IBL_BIT           (1 << 10)
#define RENDER_FLAG_EMISSIVE_BIT      (1 << 11)

// --- 2. Data Structures ---

struct GpuMaterial
{
    vec3 emission;
    float roughness;
    
    vec3 colour;
    float metallic;
    
    float padding;
    float anisotropy;
    float materialType;
    float opacity;

    vec3 scatteringColour;
    float transmissionDepth;

    int emissionTexture;
    int colourTexture;
    int roughnessTexture;
    int normalTexture;
};

struct GpuAABB
{
    vec3 min;
    float pad0;
    vec3 max;
    float pad1;
};

struct GpuInstance
{
    mat4 transform;
    mat4 inverseTransform;
    mat4 normalTransform;
    GpuAABB bounds;

    uint shape;
    uint index;
    uint material;
    uint selected;

    // Chimera extensions for buffer addresses
    uint64 vertexAddress;
    uint64 indexAddress;
    mat4 prevTransform;
};

struct GpuTriangle
{
    vec4 positionUvX0;
    vec4 positionUvX1;
    vec4 positionUvX2;
    
    vec4 normalUvY0; 
    vec4 normalUvY1; 
    vec4 normalUvY2;
    
    vec4 tangent0;
    vec4 tangent1;  
    vec4 tangent2;
    
    vec3 triCenter;
    float padding3; 
};

struct GpuVertex
{
    vec3 pos;
    vec3 normal;
    vec4 tangent;
    vec2 texCoord;
};

struct GpuLight
{
    int instance;
    int cdfCount;
    int environment;
    int cdfStart;
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
    LightData sunLight;

    vec4 displayData; // x: width, y: height, z: 1/width, w: 1/height
    uvec4 frameData; // x: frameIndex, y: frameCount, z: displayMode, w: renderFlags
    vec4 postData; // x: exposure, y: ambientStrength, zw: blueNoiseTextureIndex
    vec4 envData; // x: skyboxTextureIndex, yzw: padding
    vec4 svgfAlpha; // x: alphaColor, y: alphaMoments, zw: padding
    vec4 svgfPhi; // x: phiColor, y: phiNormal, z: phiDepth, w: padding
    vec4 gpuClearColor;
};

#ifndef __cplusplus
layout(set = 0, binding = BINDING_GLOBAL_UBO) uniform GlobalUBO
{
    CameraData camera;
    LightData sunLight;
    vec4 displayData;
    uvec4 frameData;
    vec4 postData;
    vec4 envData;
    vec4 svgfAlpha;
    vec4 svgfPhi;
    vec4 gpuClearColor;
};
#endif

struct HitPayload
{
    vec4 color_dist;
    vec4 normal_rough;
    vec4 motion_hit;
};

struct ScenePushConstants
{
    uint objectId;
};

#ifdef __cplusplus
// Alignment and size checks
static_assert(sizeof(GpuMaterial) == 80, "GpuMaterial size mismatch");
static_assert(sizeof(GpuTriangle) == 160, "GpuTriangle size mismatch");
static_assert(sizeof(GpuLight) == 16, "GpuLight size mismatch");
static_assert(sizeof(UniformBufferObject) % 16 == 0, "UBO alignment mismatch");
} // namespace Chimera
#endif

#endif
