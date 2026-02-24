#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference2 : require

#include "ShaderCommon.h"

layout(buffer_reference, scalar) readonly buffer VertexBufferRef { GpuVertex v[]; };
layout(buffer_reference, scalar) readonly buffer IndexBufferRef { uint i[]; };

layout(location = 0) rayPayloadInEXT HitPayload payload;
hitAttributeEXT vec2 attribs;

layout(set = 1, binding = 0) uniform accelerationStructureEXT topLevelAS;
layout(set = 1, binding = 1, scalar) readonly buffer MaterialBuffer { GpuMaterial m[]; } materialBuffer;

// [MODERN] Unified Primitive SSBO
layout(set = 1, binding = 2, scalar) readonly buffer PrimitiveBuffer 
{
    GpuPrimitive primitives[];
} primBuf;

layout(set = 1, binding = 3) uniform sampler2D textureArray[];

void main() 
{
    payload.hit = true;
    
    // Everything is now derived from the unified SSBO using the instance index
    uint objId = gl_InstanceCustomIndexEXT;
    GpuPrimitive prim = primBuf.primitives[objId];
    GpuMaterial mat = materialBuffer.m[prim.materialIndex];
    
    IndexBufferRef indices = IndexBufferRef(prim.indexAddress);
    uint i0 = indices.i[3 * gl_PrimitiveID];
    uint i1 = indices.i[3 * gl_PrimitiveID + 1];
    uint i2 = indices.i[3 * gl_PrimitiveID + 2];

    VertexBufferRef vertices = VertexBufferRef(prim.vertexAddress);
    GpuVertex v0 = vertices.v[i0];
    GpuVertex v1 = vertices.v[i1];
    GpuVertex v2 = vertices.v[i2];

    const vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    vec2 uv = v0.texCoord * barycentrics.x + v1.texCoord * barycentrics.y + v2.texCoord * barycentrics.z;

    // 1. Albedo
    vec3 color = mat.albedo.rgb;
    if (mat.albedoTex >= 0) 
    {
        color *= texture(textureArray[nonuniformEXT(mat.albedoTex)], uv).rgb;
    }
    
    // 2. Normal Mapping
    vec3 vertexNormal = normalize(v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z);
    vec3 N = normalize(mat3(prim.normalMatrix) * vertexNormal);

    if (mat.normalTex >= 0) 
    {
        vec3 T = normalize(mat3(prim.normalMatrix) * (v0.tangent.xyz * barycentrics.x + v1.tangent.xyz * barycentrics.y + v2.tangent.xyz * barycentrics.z));
        float w = v0.tangent.w * barycentrics.x + v1.tangent.w * barycentrics.y + v2.tangent.w * barycentrics.z;
        vec3 B = normalize(cross(N, T) * w);
        mat3 TBN = mat3(T, B, N);
        vec3 mappedNormal = texture(textureArray[nonuniformEXT(mat.normalTex)], uv).rgb * 2.0 - 1.0;
        N = normalize(TBN * mappedNormal);
    }

    // 3. Roughness
    float roughness = mat.roughness;
    if (mat.metalRoughTex >= 0) 
    {
        roughness *= texture(textureArray[nonuniformEXT(mat.metalRoughTex)], uv).g;
    }
    
    payload.color = color;
    payload.normal = N;
    payload.distance = gl_HitTEXT;
    payload.roughness = roughness;
}
