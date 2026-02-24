#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_ray_query : enable
#include "ShaderCommon.h"

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inWorldPos;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in flat uint inObjectId; 

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform GlobalUBO { UniformBufferObject ubo; } global;

layout(set = 1, binding = 0) uniform accelerationStructureEXT TLAS; 
layout(set = 1, binding = 1, scalar) readonly buffer MaterialBuffer { GpuMaterial m[]; } materialBuffer;

layout(set = 1, binding = 2, scalar) readonly buffer PrimitiveBuffer 
{
    GpuPrimitive primitives[];
} primBuf;

layout(set = 1, binding = 3) uniform sampler2D textureArray[];

void main() 
{
    GpuPrimitive prim = primBuf.primitives[inObjectId];
    GpuMaterial mat = materialBuffer.m[prim.materialIndex];
    
    vec4 albedo = mat.albedo;
    if (mat.albedoTex >= 0) 
    {
        albedo *= texture(textureArray[nonuniformEXT(mat.albedoTex)], inUV);
    }

    // --- TBN Normal Mapping ---
    vec3 N = normalize(inNormal);
    if (mat.normalTex >= 0)
    {
        vec3 T = normalize(mat3(prim.normalMatrix) * inTangent.xyz);
        float w = inTangent.w;
        vec3 B = normalize(cross(N, T) * w);
        mat3 TBN = mat3(T, B, N);
        vec3 mapNormal = texture(textureArray[nonuniformEXT(mat.normalTex)], inUV).xyz * 2.0 - 1.0;
        N = normalize(TBN * mapNormal);
    }

    vec3 lightDir = normalize(-global.ubo.sunLight.direction.xyz);
    
    // --- RAY QUERY SHADOWS ---
    float shadowFactor = 1.0;
    vec3 rayOrigin = inWorldPos + N * 0.001; 
    
    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, TLAS, gl_RayFlagsTerminateOnFirstHitEXT, 0xFF, rayOrigin, 0.001, lightDir, 10000.0);
    while(rayQueryProceedEXT(rq)) { } 
    
    if(rayQueryGetIntersectionTypeEXT(rq, true) != gl_RayQueryCommittedIntersectionNoneEXT)
    {
        shadowFactor = 0.0; 
    }

    float diff = max(dot(N, lightDir), 0.0);
    vec3 diffuse = diff * global.ubo.sunLight.color.rgb * global.ubo.sunLight.intensity.x * shadowFactor;
    vec3 ambient = vec3(0.1) * albedo.rgb;
    
    outColor = vec4(ambient + diffuse * albedo.rgb, albedo.a);
}
