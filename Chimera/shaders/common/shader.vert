#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragPos;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model; // Must match C++ struct exactly
    mat4 view;
    mat4 proj;
    mat4 prevView;
    mat4 prevProj;
    vec4 lightPos;
    int frameCount;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
} push;

void main() {
    // Use push constant for model matrix as it's updated per-node
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;

    fragNormal = mat3(transpose(inverse(push.model))) * inNormal;
    fragTexCoord = inTexCoord;
    fragPos = worldPos.xyz;
}