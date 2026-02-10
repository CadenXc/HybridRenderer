#ifndef SHADER_COMMON_H
#define SHADER_COMMON_H

#ifdef __cplusplus
#include <glm/glm.hpp>
#include <cstdint>
using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;
using mat4 = glm::mat4;
using uint = uint32_t;
#else
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_scalar_block_layout : enable
#endif

// --- Shading Structures ---

struct DirectionalLight
{
    mat4 projview;
    vec4 direction;
    vec4 color;
    vec4 intensity;
};

struct UniformBufferObject
{
    // --- Current Frame Camera (Rendering) ---
    mat4 view;
    mat4 proj;
    vec4 cameraPos;

    // --- Inverse Matrices (Position Reconstruction / Ray Generation) ---
    mat4 viewInverse;
    mat4 projInverse;
    mat4 viewProjInverse;

    // --- Previous Frame State (Temporal Reprojection / SVGF / TAA) ---
    mat4 prevView;
    mat4 prevProj;

    // --- Environment & Lighting (Shading) ---
    DirectionalLight directionalLight;

    // --- Screen Properties & System State (Sampling / Debug / UVs) ---
    vec2 displaySize;
    vec2 displaySizeInverse;
    uint frameIndex;
    uint frameCount;
    uint displayMode;
    float padding;
};

#ifndef __cplusplus
// --- Shader Utility Functions (GLSL only) ---

/**
 * @brief Reconstructs world-space position from a depth value and screen UV.
 * Uses the Inverse View-Projection matrix from the camera.
 */
vec3 GetWorldPos(float depth, vec2 uv, mat4 invViewProj)
{
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 worldPos = invViewProj * clipPos;
    return worldPos.xyz / worldPos.w;
}

/**
 * @brief Reconstructs view-space position from a depth value and screen UV.
 * Uses the Inverse Projection matrix.
 */
vec3 GetViewPos(float depth, vec2 uv, mat4 invProj)
{
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = invProj * clipPos;
    return viewPos.xyz / viewPos.w;
}
#endif

// --- Scene & Geometry Structures ---

struct PBRMaterial {
    vec4 albedo;
    vec4 emission;
    float roughness;
    float metallic;
    int albedoTex;
    int normalTex;
    int metalRoughTex;
    int padding[3];
};

struct RTInstanceData
{
    uint64_t vertexAddress;
    uint64_t indexAddress;
    int materialIndex;
    int padding;
};

// --- Push Constants ---

struct GBufferPushConstants {
    mat4 model;
    mat4 normalMatrix;
    int materialIndex;
};

struct ForwardPushConstants {
    mat4 model;
    mat4 normalMatrix;
    int materialIndex;
};

struct RaytracePushConstants {
    vec4 clearColor;
    vec3 lightPos;
    float lightIntensity;
    int frameCount;
    int skyboxIndex;
};

struct Vertex {
    vec3 pos;
    float pad1;
    vec3 normal;
    float pad2;
    vec4 tangent;
    vec2 texCoord;
    vec2 pad3;
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
// Use static_assert to ensure memory alignment matches Vulkan expectations
static_assert(sizeof(UniformBufferObject) % 16 == 0, "UBO size must be a multiple of 16 for std140/std430");
static_assert(sizeof(PBRMaterial) == 64, "PBRMaterial size must be 64 bytes");
#endif

#endif // SHADER_COMMON_H
