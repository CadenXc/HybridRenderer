#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : enable
#include "ShaderCommon.h"

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in flat uint inObjectId;
layout(location = 3) in vec4 inCurPos;
layout(location = 4) in vec4 inPrevPos;
layout(location = 5) in vec4 inTangent;

layout(set = 1, binding = 1, scalar) readonly buffer MaterialBuffer { GpuMaterial m[]; } materialBuffer;

// [NEW] SSBO Binding for Primitives
layout(set = 1, binding = 2, scalar) readonly buffer PrimitiveBuffer 
{
    GpuPrimitive primitives[];
} primBuf;

layout(set = 1, binding = 3) uniform sampler2D textureArray[];

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outMaterial;
layout(location = 3) out vec4 outMotion;

void main() 
{
    GpuPrimitive prim = primBuf.primitives[inObjectId];
    GpuMaterial mat = materialBuffer.m[prim.materialIndex];
    
    // 1. Albedo
    vec4 albedo = mat.albedo;
    if (mat.albedoTex >= 0) 
    {
        albedo *= texture(textureArray[nonuniformEXT(mat.albedoTex)], inTexCoord);
    }
    if (albedo.a < 0.1) 
    {
        discard;
    }

    // 2. Normal Mapping
    vec3 N = normalize(inNormal);
    if (mat.normalTex >= 0) 
    {
        vec3 tangent = normalize(inTangent.xyz);
        vec3 bitangent = normalize(cross(N, tangent) * inTangent.w);
        mat3 TBN = mat3(tangent, bitangent, N);
        
        vec3 mappedNormal = texture(textureArray[nonuniformEXT(mat.normalTex)], inTexCoord).rgb;
        mappedNormal = mappedNormal * 2.0 - 1.0;
        N = normalize(TBN * mappedNormal);
    }

    // 3. Metallic-Roughness
    float metallic = mat.metallic;
    float roughness = mat.roughness;
    if (mat.metalRoughTex >= 0) 
    {
        vec4 mrSample = texture(textureArray[nonuniformEXT(mat.metalRoughTex)], inTexCoord);
        roughness *= mrSample.g;
        metallic *= mrSample.b;
    }

    // 4. Motion Vector
    vec2 a = (inCurPos.xy / inCurPos.w) * 0.5 + 0.5;
    vec2 b = (inPrevPos.xy / inPrevPos.w) * 0.5 + 0.5;
    outMotion = vec4(a - b, 0.0, 1.0);

    outAlbedo = vec4(albedo.rgb, 1.0);
    outNormal = vec4(N * 0.5 + 0.5, 1.0);
    outMaterial = vec4(roughness, metallic, 0.0, 1.0);
}
