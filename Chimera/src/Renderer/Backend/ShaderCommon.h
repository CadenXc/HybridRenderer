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

// --- GLSL Helper Functions ---
vec3 GetWorldPos(float depth, vec2 uv, mat4 invViewProj) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 world = invViewProj * clip;
    return world.xyz / world.w;
}

vec3 GetViewPos(float depth, vec2 uv, mat4 invProj) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 view = invProj * clip;
    return view.xyz / view.w;
}
#endif

struct DirectionalLight {
    mat4 projview;
    vec4 direction;
    vec4 color;
    vec4 intensity;
};

struct UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec4 cameraPos;
    mat4 viewInverse;
    mat4 projInverse;
    mat4 viewProjInverse;
    mat4 prevView;
    mat4 prevProj;
    DirectionalLight directionalLight;
    vec2 displaySize;
    vec2 displaySizeInverse;
    uint frameIndex;
    uint frameCount;
    uint displayMode; // 0: Final, 1: Albedo, 2: Normal, 3: Material, 4: Motion, 5: Depth, 6: ShadowAO, 7: Reflection
    uint renderFlags;  // Bit 0: Enable SVGF, Bit 1: Enable GI, Bit 2: Show Denoising Variance
    float exposure;
    float ambientStrength;
    float bloomStrength;

    // --- SVGF & Light Parameters ---
    float svgfAlphaColor;
    float svgfAlphaMoments;
    float svgfPhiColor;
    float svgfPhiNormal;
    float svgfPhiDepth;
    float lightRadius;
    float padding1;
    float padding2;
};

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

struct GBufferPushConstants {
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

// [RESTORED] 保持 Vertex 命名以兼容 SceneCommon.h
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
static_assert(sizeof(PBRMaterial) == 64, "PBRMaterial size must be 64 bytes");
static_assert(sizeof(Vertex) == 64, "Vertex size must be 64 bytes");
#endif

#endif
