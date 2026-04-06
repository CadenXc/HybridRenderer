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

// Set 1: Scene Resources
layout(set = 1, binding = BINDING_AS) uniform accelerationStructureEXT TLAS; 

layout(set = 1, binding = BINDING_MATERIALS, scalar) readonly buffer MaterialBuffer 
{
    GpuMaterial materials[]; 
};

layout(set = 1, binding = BINDING_PRIMITIVES, scalar) readonly buffer PrimitiveBuffer 
{
    GpuPrimitive primitives[];
};

layout(set = 1, binding = BINDING_TEXTURES) uniform sampler2D textureArray[];

// Buffer references for manual fetching in Hit Shaders
layout(buffer_reference, scalar) readonly buffer VertexBufferRef { GpuVertex v[]; };
layout(buffer_reference, scalar) readonly buffer IndexBufferRef { uint i[]; };

// --- 4. Utility Functions ---

const float PI = 3.14159265359;
const float COS_PI_4 = 0.70710678118; // cos(45 degrees) for normal validation

// 4.0 Random & Sampling Utilities
float Halton(uint index, uint base) 
{
    float result = 0.0;
    float f = 1.0 / float(base);
    uint i = index;
    while (i > 0) 
    {
        result += f * float(i % base);
        i /= base;
        f /= float(base);
    }
    return result;
}

vec2 GetHaltonSample(uint index)
{
    return vec2(Halton(index, 2), Halton(index, 3));
}

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

float RandomFloat(inout uint seed) 
{
    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return float(seed) / 4294967296.0;
}

vec3 GetCosHemisphereSample(inout uint seed, vec3 normal) 
{
    float r1 = RandomFloat(seed);
    float r2 = RandomFloat(seed);
    float r = sqrt(r1);
    float phi = 2.0 * PI * r2;
    vec3 tangent = normalize(cross(normal, abs(normal.x) > 0.1 ? vec3(0, 1, 0) : vec3(1, 0, 0)));
    vec3 bitangent = cross(normal, tangent);
    return normalize(tangent * r * cos(phi) + bitangent * r * sin(phi) + normal * sqrt(1.0 - r1));
}

vec3 GetWorldPos(float depth, vec2 uv, mat4 invViewProj)
{
    vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 world = invViewProj * clip;
    return world.xyz / world.w;
}

vec2 SampleEquirectangular(vec3 v)
{
    // Equirectangular mapping:
    // phi = atan2(z, x), theta = asin(y)
    float phi = atan(v.z, v.x);
    float theta = asin(v.y);
    
    vec2 uv;
    uv.x = (phi / (2.0 * PI)) + 0.5;
    uv.y = (theta / PI) + 0.5;
    
    // Most HDRs need V to be 0 at the top (theta = PI/2)
    // and 1 at the bottom (theta = -PI/2)
    uv.y = 1.0 - uv.y; 
    
    return uv;
}

// 4.1 Vertex Transformation Utilities
vec4 LocalToWorld(vec3 pos, mat4 transform)
{
    return transform * vec4(pos, 1.0);
}

vec4 WorldToClip(vec4 worldPos)
{
    return camera.proj * camera.view * worldPos;
}

vec4 PrevWorldToClip(vec4 prevWorldPos)
{
    return camera.prevProj * camera.prevView * prevWorldPos;
}

// 4.2 Ray Tracing Utilities
struct Ray { vec3 origin; vec3 dir; };

float CalculateRayQueryShadow(vec3 origin, vec3 L, float maxDist) 
{
    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, TLAS, gl_RayFlagsTerminateOnFirstHitEXT, 0xFF, origin, 0.001, L, maxDist);
    while (rayQueryProceedEXT(rq)) 
    {
        if (rayQueryGetIntersectionTypeEXT(rq, false) == gl_RayQueryCandidateIntersectionTriangleEXT) 
        {
            uint objId = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, false);
            uint primIdx = rayQueryGetIntersectionPrimitiveIndexEXT(rq, false);
            vec2 bary = rayQueryGetIntersectionBarycentricsEXT(rq, false);
            GpuPrimitive prim = primitives[objId];
            GpuMaterial mat = materials[prim.materialIndex];
            if (mat.albedoTex >= 0) 
            {
                VertexBufferRef vBuf = VertexBufferRef(prim.vertexAddress);
                IndexBufferRef iBuf = IndexBufferRef(prim.indexAddress);
                uint i0 = iBuf.i[primIdx * 3 + 0];
                uint i1 = iBuf.i[primIdx * 3 + 1];
                uint i2 = iBuf.i[primIdx * 3 + 2];
                vec2 uv = vBuf.v[i0].texCoord * (1.0 - bary.x - bary.y) + vBuf.v[i1].texCoord * bary.x + vBuf.v[i2].texCoord * bary.y;
                if (texture(textureArray[nonuniformEXT(mat.albedoTex)], uv).a < 0.5) continue;
            }
            return 0.0;
        }
    }
    return 1.0;
}

vec3 OffsetRay(vec3 p, vec3 n) 
{
    const float origin = 1.0 / 32.0;
    const float float_scale = 1.0 / 65536.0;
    const float int_scale = 256.0;
    ivec3 of_i = ivec3(int_scale * n.x, int_scale * n.y, int_scale * n.z);
    vec3 p_i = vec3(intBitsToFloat(floatBitsToInt(p.x) + ((p.x < 0) ? -of_i.x : of_i.x)),
                    intBitsToFloat(floatBitsToInt(p.y) + ((p.y < 0) ? -of_i.y : of_i.y)),
                    intBitsToFloat(floatBitsToInt(p.z) + ((p.z < 0) ? -of_i.z : of_i.z)));
    return vec3(abs(p.x) < origin ? p.x + float_scale * n.x : p_i.x,
                abs(p.y) < origin ? p.y + float_scale * n.y : p_i.y,
                abs(p.z) < origin ? p.z + float_scale * n.z : p_i.z);
}

// 4.2 Material & PBR Utilities
vec4 GetAlbedo(GpuMaterial mat, vec2 uv) 
{
    vec4 base = mat.albedo;
    if (mat.albedoTex >= 0) 
    {
        base *= texture(textureArray[nonuniformEXT(mat.albedoTex)], uv);
    }
    return base;
}

// 4.2 Sampling & Noise Utilities
vec4 GetBlueNoise(ivec2 coord)
{
    int blueNoiseIdx = int(postData.w);
    if (blueNoiseIdx < 0) return vec4(0.0);
    ivec2 noiseSize = textureSize(textureArray[nonuniformEXT(blueNoiseIdx)], 0);
    return texture(textureArray[nonuniformEXT(blueNoiseIdx)], (vec2(coord) + 0.5) / vec2(noiseSize));
}

vec3 SquareToUniformCone(vec2 u, float cosThetaMax)
{
    float cosTheta = (1.0 - u.x) + u.x * cosThetaMax;
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    float phi = u.y * 2.0 * PI;
    return vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

vec3 CalculateNormal(GpuMaterial mat, vec3 N, vec4 tangent, vec2 uv) 
{
    if (mat.normalTex < 0) return normalize(N);
    
    // Guard against zero-length tangents which cause NaN
    vec3 T = normalize(tangent.xyz);
    if (length(tangent.xyz) < 0.001) {
        return normalize(N);
    }

    vec3 B = cross(N, T) * (abs(tangent.w) < 0.001 ? 1.0 : tangent.w);
    mat3 TBN = mat3(T, B, N);
    vec3 nm = texture(textureArray[nonuniformEXT(mat.normalTex)], uv).xyz * 2.0 - 1.0;
    return normalize(TBN * nm);
}

float GetAmbientOcclusion(GpuMaterial mat, vec2 uv) 
{
    if (mat.aoTex < 0)
    {
        return 1.0;
    }

    return texture(textureArray[nonuniformEXT(mat.aoTex)], uv).r;
}

vec3 GetEmissive(GpuMaterial mat, vec2 uv) 
{
    vec3 e = mat.emission.rgb;
    if (mat.emissiveTex >= 0)
    {
        e *= texture(textureArray[nonuniformEXT(mat.emissiveTex)], uv).rgb;
    }
    
    return e;
}

vec3 ACESToneMapping(vec3 color) 
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) 
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float DistributionGGX(float NoH, float roughness) 
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NoH2 = NoH * NoH;
    float denom = (NoH2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

float GeometrySchlickGGX(float NoV, float NoL, float roughness) 
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    float g1 = NoV / (NoV * (1.0 - k) + k);
    float g2 = NoL / (NoL * (1.0 - k) + k);
    return g1 * g2;
}

// 4.3 Kulla-Conty Multi-Scattering Compensation
// Analytical approximation of the integrated BRDF (Directional Albedo)
float DirectionalAlbedo(float NoV, float roughness)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a = min(r.x * r.y, exp2(-9.28 * NoV)) * r.z + r.w;
    return clamp(a, 0.0, 1.0);
}

vec3 EvaluateDirectPBR(vec3 worldNormal, vec3 viewDirection, vec3 lightDirection, vec3 baseColor, float roughness, float metallic, vec3 lightIntensity) 
{
    vec3 halfVector = normalize(viewDirection + lightDirection);
    float NoV = max(dot(worldNormal, viewDirection), 0.0001);
    float NoL = max(dot(worldNormal, lightDirection), 0.0);
    float NoH = max(dot(worldNormal, halfVector), 0.0);
    float HoV = max(dot(halfVector, viewDirection), 0.0);
    
    if (NoL <= 0.0) return vec3(0.0); // Early out for back-lit surfaces

    vec3 F0 = mix(vec3(0.04), baseColor, metallic);
    
    // 1. Single Scattering (Standard Cook-Torrance)
    float D = DistributionGGX(NoH, roughness);
    float G = GeometrySchlickGGX(NoV, NoL, roughness);
    vec3 F = FresnelSchlickRoughness(HoV, F0, roughness);
    
    // Use an epsilon to prevent division by zero and resulting NaNs
    vec3 f_single = (D * G * F) / max(4.0 * NoV * NoL, 0.0001);
    
    // 2. Multi-Scattering Compensation (Kulla-Conty)
    float Ess = DirectionalAlbedo(NoV, roughness);
    float Esl = DirectionalAlbedo(NoL, roughness);
    float Eavg = 0.6; 
    
    vec3 f_add = F0 * (1.0 - Ess) * (1.0 - Esl) / max(PI * (1.0 - Eavg), 0.0001);
    
    // 3. Combined Reflectance
    vec3 kS = F + f_add * Ess; 
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    
    return (kD * baseColor / PI + f_single + f_add) * lightIntensity * NoL;
}

#endif // CHIMERA_COMMON_GLSL
