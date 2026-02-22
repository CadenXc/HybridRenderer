#version 460
#extension GL_GOOGLE_include_directive : require
#include "ShaderCommon.h"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec2 outUV;
layout(location = 2) out vec3 outWorldPos;

layout(set = 0, binding = 0) uniform GlobalUBO 
{
    UniformBufferObject ubo;
} global;

layout(push_constant) uniform PushConstants 
{
    GBufferPushConstants pc;
};

void main() 
{
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    outWorldPos = worldPos.xyz;
    outNormal = normalize(mat3(pc.normalMatrix) * inNormal);
    outUV = inTexCoord;
    
    gl_Position = global.ubo.camera.proj * global.ubo.camera.view * worldPos;
}
