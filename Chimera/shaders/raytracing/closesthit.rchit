#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference2 : require

#include "ShaderCommon.h"

// [FIX] 确保引用的是头文件里定义的 Vertex
layout(buffer_reference, scalar) readonly buffer Vertices { Vertex v[]; };
layout(buffer_reference, scalar) readonly buffer Indices { uint i[]; };

layout(location = 0) rayPayloadInEXT HitPayload payload;
hitAttributeEXT vec2 attribs;

layout(binding = 0, set = 1) uniform accelerationStructureEXT topLevelAS;
layout(binding = 1, set = 1, scalar) readonly buffer MaterialBuffer { PBRMaterial m[]; } materialBuffer;
layout(binding = 2, set = 1, scalar) readonly buffer InstanceDataBuffer { RTInstanceData i[]; } instanceBuffer;
layout(binding = 3, set = 1) uniform sampler2D textureArray[];

void main() 
{
    payload.hit = true;
    
    RTInstanceData inst = instanceBuffer.i[gl_InstanceCustomIndexEXT];
    PBRMaterial mat = materialBuffer.m[inst.materialIndex];
    
    Indices indices = Indices(inst.indexAddress);
    uint i0 = indices.i[3 * gl_PrimitiveID];
    uint i1 = indices.i[3 * gl_PrimitiveID + 1];
    uint i2 = indices.i[3 * gl_PrimitiveID + 2];

    Vertices vertices = Vertices(inst.vertexAddress);
    Vertex v0 = vertices.v[i0];
    Vertex v1 = vertices.v[i1];
    Vertex v2 = vertices.v[i2];

    const vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    vec2 uv = v0.texCoord * barycentrics.x + v1.texCoord * barycentrics.y + v2.texCoord * barycentrics.z;

    vec3 color = mat.albedo.rgb;
    if (mat.albedoTex >= 0) {
        color *= texture(textureArray[nonuniformEXT(mat.albedoTex)], uv).rgb;
    }
    
    payload.color = color;
    payload.normal = normalize(v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z);
    payload.distance = gl_HitTEXT;
}