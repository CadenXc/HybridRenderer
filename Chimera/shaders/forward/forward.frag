#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#include "ShaderCommon.h"

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inWorldPos;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform GlobalUBO 
{
    UniformBufferObject ubo;
} global;

layout(set = 1, binding = 1, scalar) readonly buffer MaterialBuffer { GpuMaterial m[]; } materialBuffer;
layout(set = 1, binding = 3) uniform sampler2D textureArray[];

layout(push_constant) uniform PushConstants 
{
    GBufferPushConstants pc;
};

void main() 
{
    GpuMaterial mat = materialBuffer.m[pc.materialIndex];
    vec4 albedo = mat.albedo;
    if (mat.albedoTex >= 0) 
    {
        albedo *= texture(textureArray[nonuniformEXT(mat.albedoTex)], inUV);
    }

    vec3 N = normalize(inNormal);
    vec3 L = normalize(-global.ubo.sunLight.direction.xyz);
    float diff = max(dot(N, L), 0.0);
    
    vec3 diffuse = diff * global.ubo.sunLight.color.rgb * global.ubo.sunLight.intensity.x;
    vec3 ambient = vec3(0.1) * albedo.rgb;
    
    outColor = vec4(ambient + diffuse * albedo.rgb, albedo.a);
}
