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

layout(set = 1, binding = BINDING_AS) uniform accelerationStructureEXT TLAS; 

layout(set = 1, binding = BINDING_MATERIALS, scalar) readonly buffer MaterialBuffer 
{
    GpuMaterial materials[]; 
};

layout(set = 1, binding = BINDING_INSTANCES, scalar) readonly buffer InstanceBuffer 
{
    GpuInstance instances[];
};

layout(set = 1, binding = BINDING_TEXTURES) uniform sampler2D textureArray[];

layout(set = 1, binding = BINDING_LIGHTS, scalar) readonly buffer LightBuffer 
{
    GpuLight lights[];
};

layout(set = 1, binding = BINDING_LIGHTS_CDF, scalar) readonly buffer CDFBuffer 
{
    float lightsCDF[];
};

layout(buffer_reference, scalar) readonly buffer VertexBufferRef { GpuVertex v[]; };
layout(buffer_reference, scalar) readonly buffer IndexBufferRef { uint i[]; };

// --- 4. Constants & SVGF Aligned Math ---

const float PI = 3.14159265359;
const float PI_F = 3.14159265359;
const float MIN_ROUGHNESS = (0.03 * 0.03);

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

vec3 GetWorldPos(float depth, vec2 uv, mat4 invViewProj)
{
    vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 world = invViewProj * clip;
    return world.xyz / world.w;
}

vec2 SampleEquirectangular(vec3 v)
{
    float phi = atan(v.z, v.x);
    float theta = asin(clamp(v.y, -1.0, 1.0));
    vec2 uv;
    uv.x = (phi / (2.0 * PI)) + 0.5;
    uv.y = (theta / PI) + 0.5;
    uv.y = 1.0 - uv.y; 
    return uv;
}

vec4 LocalToWorld(vec3 pos, mat4 transform) { return transform * vec4(pos, 1.0); }
vec4 WorldToClip(vec4 worldPos) { return camera.proj * camera.view * worldPos; }
vec4 PrevWorldToClip(vec4 prevWorldPos) { return camera.prevProj * camera.prevView * prevWorldPos; }

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
            GpuInstance inst = instances[objId];
            GpuMaterial mat = materials[inst.material];
            if (mat.colourTexture >= 0) 
            {
                VertexBufferRef vBuf = VertexBufferRef(inst.vertexAddress);
                IndexBufferRef iBuf = IndexBufferRef(inst.indexAddress);
                uint i0 = iBuf.i[primIdx * 3 + 0];
                uint i1 = iBuf.i[primIdx * 3 + 1];
                uint i2 = iBuf.i[primIdx * 3 + 2];
                vec2 uv = vBuf.v[i0].texCoord * (1.0 - bary.x - bary.y) + vBuf.v[i1].texCoord * bary.x + vBuf.v[i2].texCoord * bary.y;
                if (texture(textureArray[nonuniformEXT(mat.colourTexture)], uv).a < 0.5) continue;
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

// 4.2 [PLAGIARISM] SVGF PBR Math Logic

vec3 EtaToReflectivity(vec3 Eta) {
    return ((Eta - 1.0) * (Eta - 1.0)) / ((Eta + 1.0) * (Eta + 1.0));
}

vec3 FresnelSchlick(vec3 Specular, vec3 Normal, vec3 Outgoing) {
    if (Specular == vec3(0.0)) return vec3(0.0);
    float cosine = dot(Normal, Outgoing);
    return Specular + (1.0 - Specular) * pow(clamp(1.0 - abs(cosine), 0.0, 1.0), 5.0);
}

float MicrofacetDistribution(float Roughness, vec3 Normal, vec3 Halfway) {
    float Cosine = dot(Normal, Halfway);
    if (Cosine <= 0.0) return 0.0;
    float Roughness2 = Roughness * Roughness;
    float Cosine2 = Cosine * Cosine;
    float denom = (Cosine2 * (Roughness2 - 1.0) + 1.0);
    return Roughness2 / (PI_F * denom * denom);
}

float MicrofacetShadowing1(float Roughness, vec3 Normal, vec3 Halfway, vec3 Direction) {
    float Cosine = dot(Normal, Direction);
    float Cosine2 = Cosine * Cosine;
    float CosineH = dot(Halfway, Direction);
    if (Cosine * CosineH <= 0.0) return 0.0;
    float Roughness2 = Roughness * Roughness;
    return 2.0 / (sqrt(((Roughness2 * (1.0 - Cosine2)) + Cosine2) / Cosine2) + 1.0);
}

float MicrofacetShadowing(float Roughness, vec3 Normal, vec3 Halfway, vec3 Outgoing, vec3 Incoming) {
    return MicrofacetShadowing1(Roughness, Normal, Halfway, Outgoing) * MicrofacetShadowing1(Roughness, Normal, Halfway, Incoming);
}

vec3 EvalPbr(vec3 Colour, float IOR, float Roughness, float Metallic, vec3 Normal, vec3 Outgoing, vec3 Incoming) {
    if (dot(Normal, Incoming) * dot(Normal, Outgoing) <= 0.0) return vec3(0.0);

    vec3 Reflectivity = mix(EtaToReflectivity(vec3(IOR)), Colour, Metallic);
    vec3 UpNormal = dot(Normal, Outgoing) <= 0.0 ? -Normal : Normal;
    vec3 F1 = FresnelSchlick(Reflectivity, UpNormal, Outgoing);
    vec3 Halfway = normalize(Incoming + Outgoing);
    vec3 F = FresnelSchlick(Reflectivity, Halfway, Incoming);
    float D = MicrofacetDistribution(Roughness, UpNormal, Halfway);
    float G = MicrofacetShadowing(Roughness, UpNormal, Halfway, Outgoing, Incoming);

    float Cosine = abs(dot(UpNormal, Incoming));
    vec3 Diffuse = Colour * (1.0 - Metallic) * (1.0 - F1) / PI_F;
    vec3 Specular = F * D * G / (4.0 * abs(dot(UpNormal, Outgoing)) * abs(dot(UpNormal, Incoming)));

    return (Diffuse + Specular) * Cosine;
}

// 4.3 [PLAGIARISM] SVGF Light Sampling

vec2 SampleTriangle(vec2 u) {
    float r = sqrt(u.x);
    return vec2(1.0 - r, u.y * r);
}

int SampleDiscrete(int lightID, float randVal) {
    int start = lights[lightID].cdfStart;
    int count = lights[lightID].cdfCount;
    float maxVal = lightsCDF[start + count - 1];
    float x = randVal * maxVal;
    
    int low = start;
    int high = start + count;
    while (low < high) {
        int mid = low + (high - low) / 2;
        if (x >= lightsCDF[mid]) low = mid + 1;
        else high = mid;
    }
    return clamp(low - start, 0, count - 1);
}

vec3 SampleLights(vec3 position, float randL, float randEl, vec2 randUV, inout int sampledLightInstance) {
    uint lightCount = uint(envData.y);
    if (lightCount == 0) return vec3(0.0);

    int lightID = int(randL * float(lightCount));
    lightID = clamp(lightID, 0, int(lightCount) - 1);

    if (lights[lightID].instance != INVALID_ID) {
        sampledLightInstance = lights[lightID].instance;
        GpuInstance inst = instances[sampledLightInstance];
        int element = SampleDiscrete(lightID, randEl);
        vec2 triUV = SampleTriangle(randUV);

        VertexBufferRef vBuf = VertexBufferRef(inst.vertexAddress);
        IndexBufferRef iBuf = IndexBufferRef(inst.indexAddress);
        uint i0 = iBuf.i[element * 3 + 0];
        uint i1 = iBuf.i[element * 3 + 1];
        uint i2 = iBuf.i[element * 3 + 2];

        vec3 p0 = (inst.transform * vec4(vBuf.v[i0].pos, 1.0)).xyz;
        vec3 p1 = (inst.transform * vec4(vBuf.v[i1].pos, 1.0)).xyz;
        vec3 p2 = (inst.transform * vec4(vBuf.v[i2].pos, 1.0)).xyz;

        vec3 lightPos = p1 * triUV.x + p2 * triUV.y + p0 * (1.0 - triUV.x - triUV.y);
        return normalize(lightPos - position);
    } else if (lights[lightID].environment != INVALID_ID) {
        float r1 = randUV.x;
        float r2 = randUV.y;
        float z = 2.0 * r1 - 1.0;
        float r = sqrt(max(0.0, 1.0 - z * z));
        float phi = 2.0 * PI * r2;
        return vec3(r * cos(phi), r * sin(phi), z);
    }
    return vec3(0.0);
}

// 4.4 Material & Texture Utilities

vec4 GetAlbedo(GpuMaterial mat, vec2 uv) 
{
    vec4 base = vec4(mat.colour, mat.opacity);
    if (mat.colourTexture >= 0) base *= texture(textureArray[nonuniformEXT(mat.colourTexture)], uv);
    return base;
}

vec3 CalculateNormal(GpuMaterial mat, vec3 N, vec4 tangent, vec2 uv) 
{
    if (mat.normalTexture < 0) return normalize(N);
    vec3 T = normalize(tangent.xyz);
    if (length(tangent.xyz) < 0.001) return normalize(N);
    vec3 B = cross(N, T) * (abs(tangent.w) < 0.001 ? 1.0 : tangent.w);
    mat3 TBN = mat3(T, B, N);
    vec3 nm = texture(textureArray[nonuniformEXT(mat.normalTexture)], uv).xyz * 2.0 - 1.0;
    return normalize(TBN * nm);
}

vec3 GetEmissive(GpuMaterial mat, vec2 uv) 
{
    vec3 e = mat.emission;
    if (mat.emissionTexture >= 0) e *= texture(textureArray[nonuniformEXT(mat.emissionTexture)], uv).rgb;
    return e;
}

struct MaterialPoint {
    vec3 Colour;
    vec3 Emission;
    float Roughness;
    float Metallic;
    float Opacity;
    int MaterialType;
};

MaterialPoint GetMaterialPoint(GpuMaterial mat, vec2 uv) {
    MaterialPoint p;
    vec4 albedo = GetAlbedo(mat, uv);
    p.Colour = albedo.rgb;
    p.Opacity = albedo.a;
    p.Emission = GetEmissive(mat, uv);
    
    float roughness = mat.roughness;
    float metallic = mat.metallic;
    if (mat.roughnessTexture >= 0) {
        vec4 mrSample = texture(textureArray[nonuniformEXT(mat.roughnessTexture)], uv);
        roughness *= mrSample.g;
        metallic *= mrSample.b;
    }
    
    // SVGF Squared Roughness logic
    p.Roughness = roughness * roughness;
    if (p.Roughness < MIN_ROUGHNESS) p.Roughness = 0.0;
    p.Metallic = metallic;
    p.MaterialType = int(mat.materialType);
    return p;
}

#endif // CHIMERA_COMMON_GLSL
