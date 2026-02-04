#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec4 fragCurrPos;
layout(location = 3) out vec4 fragPrevPos;
layout(location = 4) out flat int outMaterialIndex;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 prevView;
    mat4 prevProj;
    vec4 cameraPos;
    vec4 lightPos;
    float time;
    int frameCount;
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

    fragCurrPos = gl_Position;
    // For now assume objects are static (prevModel == currentModel)
    fragPrevPos = ubo.prevProj * ubo.prevView * worldPos; 
    
    outMaterialIndex = push.materialIndex;
}