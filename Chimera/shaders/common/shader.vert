#version 450
#extension GL_GOOGLE_include_directive : require
#include "ShaderCommon.h"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragPos;

layout(set = 0, binding = 0) uniform GlobalUBO {
    UniformBufferObject cam;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 normalMatrix;
    int materialIndex;
} push;

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    gl_Position = ubo.cam.proj * ubo.cam.view * worldPos;

    fragNormal = mat3(push.normalMatrix) * inNormal;
    fragTexCoord = inTexCoord;
    fragPos = worldPos.xyz;
}