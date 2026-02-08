#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragPos;

struct DirectionalLight
{
    mat4 projview;
    vec4 direction;
    vec4 color;
    vec4 intensity;
};

layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
    mat4 viewInverse;
    mat4 projInverse;
    mat4 viewProjInverse;
    mat4 prevView;
    mat4 prevProj;
    DirectionalLight directionalLight;
    vec2 displaySize;
    vec2 displaySizeInverse;
    uint frameIndex;
    uint frameCount;
    uint displayMode;
    vec4 cameraPos;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 normalMatrix;
    int materialIndex;
} push;

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;

    fragNormal = mat3(push.normalMatrix) * inNormal;
    fragTexCoord = inTexCoord;
    fragPos = worldPos.xyz;
}
