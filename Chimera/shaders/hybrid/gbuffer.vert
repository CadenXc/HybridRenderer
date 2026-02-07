#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;

layout(set = 0, binding = 0) uniform GlobalUniforms {
    mat4 view;
    mat4 proj;
    mat4 viewInverse;
    mat4 projInverse;
    mat4 viewProjInverse;
    mat4 prevView;
    mat4 prevProj;
    vec4 cameraPos;
} ubo;

layout(push_constant) uniform ModelData {
    mat4 model;
    uint materialIndex;
} pc;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outTexCoord;
layout(location = 3) out mat3 outTBN;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    outWorldPos = worldPos.xyz;
    outTexCoord = inTexCoord;

    // Normal in world space
    mat3 normalMatrix = transpose(inverse(mat3(pc.model)));
    outNormal = normalize(normalMatrix * inNormal);

    // TBN Matrix calculation
    vec3 T = normalize(normalMatrix * inTangent.xyz);
    vec3 N = outNormal;
    // Gram-Schmidt process to re-orthogonalize T with respect to N
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T) * inTangent.w;
    outTBN = mat3(T, B, N);

    gl_Position = ubo.proj * ubo.view * worldPos;
}
