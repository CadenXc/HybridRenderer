#ifndef CHIMERA_COMMON_GLSL
#define CHIMERA_COMMON_GLSL

// --- 1. Required Extensions ---
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_ray_query : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// --- 2. Shared Structures (CPU/GPU) ---
#include "ShaderCommon.h"

// --- 3. Global Resource Bindings (Set 0 & 1) ---

// Set 0: Global Uniforms
layout(set = 0, binding = BINDING_GLOBAL_UBO) uniform GlobalUBO 
{
    UniformBufferObject ubo;
} global;

// Set 1: Scene Resources
layout(set = 1, binding = BINDING_AS) uniform accelerationStructureEXT TLAS; 

layout(set = 1, binding = BINDING_MATERIALS, scalar) readonly buffer MaterialBuffer 
{
    GpuMaterial m[]; 
} materialBuffer;

layout(set = 1, binding = BINDING_PRIMITIVES, scalar) readonly buffer PrimitiveBuffer 
{
    GpuPrimitive primitives[];
} primBuf;

layout(set = 1, binding = BINDING_TEXTURES) uniform sampler2D textureArray[];

// Buffer references for manual fetching in Hit Shaders
layout(buffer_reference, scalar) readonly buffer VertexBufferRef { GpuVertex v[]; };
layout(buffer_reference, scalar) readonly buffer IndexBufferRef { uint i[]; };

// --- 4. Utility Functions ---

const float PI = 3.14159265359;

// 4.0 Random & Sampling Utilities
uint InitRandomSeed(uint val0, uint val1) 
{
    uint v0 = val0;
    uint v1 = val1;
    uint s0 = 0;
    for (uint n = 0; n < 16; n++) 
    {
        s0 += 0x9e3779b9;
        v0 += ((v1 << 4) + 0xa341316c) ^ (v1 + s0) ^ ((v1 >> 5) + 0xc8013ea4);
        v1 += ((v0 << 4) + 0xad90777d) ^ (v0 + s0) ^ ((v0 >> 5) + 0x7e95761e);
    }
    return v0;
}

uint NextRandom(inout uint seed) 
{
    seed = (1664525u * seed + 1013904223u);
    return seed;
}

float RandomFloat(inout uint seed) 
{
    return float(NextRandom(seed) & 0x00FFFFFFu) / float(0x01000000u);
}

vec3 SquareToUniformCone(vec2 sampleIn, float cosThetaMax) 
{
    float cosTheta = (1.0 - sampleIn.x) + sampleIn.x * cosThetaMax;
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    float phi = sampleIn.y * 2.0 * PI;
    return vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

vec3 GetCosHemisphereSample(inout uint seed, vec3 normal) 
{
    float r1 = RandomFloat(seed);
    float r2 = RandomFloat(seed);
    float r = sqrt(r1);
    float phi = 2.0 * PI * r2;
    vec3 localDir = vec3(r * cos(phi), r * sin(phi), sqrt(max(0.0, 1.0 - r1)));
    
    vec3 up = abs(normal.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);
    return tangent * localDir.x + bitangent * localDir.y + normal * localDir.z;
}

// 4.1 PBR Math Utilities (Cook-Torrance BRDF)
float D_GGX(float NoH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NoH2 = NoH * NoH;
    float denom = (NoH2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

float G_SchlickGGX(float NoV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    float num = NoV;
    float denom = NoV * (1.0 - k) + k;
    return num / denom;
}

float G_Smith(float NoV, float NoL, float roughness)
{
    return G_SchlickGGX(NoV, roughness) * G_SchlickGGX(NoL, roughness);
}

vec3 F_Schlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 F_SchlickRoughness(float cosTheta, vec3 F0, float roughness) 
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 EvaluateDirectPBR(vec3 N, vec3 V, vec3 L, vec3 albedo, float roughness, float metallic, vec3 lightColor)
{
    vec3 H = normalize(V + L);
    float NoV = max(dot(N, V), 0.001);
    float NoL = max(dot(N, L), 0.001);
    float NoH = max(dot(N, H), 0.0);
    float HoV = max(dot(H, V), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float D = D_GGX(NoH, roughness);
    float G = G_Smith(NoV, NoL, roughness);
    vec3  F = F_Schlick(HoV, F0);

    vec3 specular = (D * G * F) / max(4.0 * NoV * NoL, 0.001);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);	

    return (kD * albedo / PI + specular) * lightColor * NoL;
}

// 4.2 Material & Shading Utilities
vec4 GetAlbedo(GpuMaterial mat, vec2 uv)
{
    vec4 albedo = mat.albedo;
    if (mat.albedoTex >= 0) 
    {
        albedo *= texture(textureArray[nonuniformEXT(mat.albedoTex)], uv);
    }
    return albedo;
}

vec3 CalculateNormal(GpuPrimitive prim, GpuMaterial mat, vec3 inNormal, vec4 inTangent, vec2 uv)
{
    vec3 N = normalize(inNormal);
    if (mat.normalTex < 0) 
    {
        return N;
    }
    vec3 T = normalize(mat3(prim.normalMatrix) * inTangent.xyz);
    vec3 B = normalize(cross(N, T) * inTangent.w);
    mat3 TBN = mat3(T, B, N);
    vec3 mapNormal = texture(textureArray[nonuniformEXT(mat.normalTex)], uv).xyz * 2.0 - 1.0;
    return normalize(TBN * mapNormal);
}

// 4.3 Ray Query & Post-Processing Helpers
float CalculateRayQueryShadow(vec3 origin, vec3 lightDir, float maxDist)
{
    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, TLAS, gl_RayFlagsTerminateOnFirstHitEXT, 0xFF, origin, 0.001, lightDir, maxDist);
    while (rayQueryProceedEXT(rq)) 
    { 
    } 
    return (rayQueryGetIntersectionTypeEXT(rq, true) != gl_RayQueryCommittedIntersectionNoneEXT) ? 0.0 : 1.0;
}

vec3 ACESToneMapping(vec3 x) 
{
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec2 SampleEquirectangular(vec3 v) 
{
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= vec2(0.1591, 0.3183); 
    uv += 0.5;
    return uv;
}

// 4.4 Geometry & Transformation Utilities
vec4 LocalToWorld(vec3 localPos, mat4 model) 
{ 
    return model * vec4(localPos, 1.0); 
}

vec4 ProjectPosition(vec3 localPos, mat4 model) 
{ 
    return global.ubo.camera.proj * global.ubo.camera.view * model * vec4(localPos, 1.0); 
}

vec4 ProjectPreviousPosition(vec3 localPos, mat4 prevModel) 
{ 
    return global.ubo.camera.prevProj * global.ubo.camera.prevView * prevModel * vec4(localPos, 1.0); 
}

// 统一的射线偏移函数 (用于防止阴影自相交/Shadow Acne)
vec3 OffsetRay(vec3 p, vec3 n)
{
    const float origin = 1.0 / 32.0;
    const float float_scale = 1.0 / 65536.0;
    const float int_scale = 256.0;

    ivec3 of_i = ivec3(int_scale * n.x, int_scale * n.y, int_scale * n.z);

    vec3 p_i = vec3(
        intBitsToFloat(floatBitsToInt(p.x) + ((p.x < 0) ? -of_i.x : of_i.x)),
        intBitsToFloat(floatBitsToInt(p.y) + ((p.y < 0) ? -of_i.y : of_i.y)),
        intBitsToFloat(floatBitsToInt(p.z) + ((p.z < 0) ? -of_i.z : of_i.z))
    );

    return vec3(
        abs(p.x) < origin ? p.x + float_scale * n.x : p_i.x,
        abs(p.y) < origin ? p.y + float_scale * n.y : p_i.y,
        abs(p.z) < origin ? p.z + float_scale * n.z : p_i.z
    );
}

#endif // CHIMERA_COMMON_GLSL
