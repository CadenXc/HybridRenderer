#version 450

layout(location = 0) out vec2 outUV;

struct DirectionalLight
{
    mat4 projview;
    vec4 direction;
    vec4 color;
    vec4 intensity;
};

layout(set = 0, binding = 0) uniform UniformBufferObject {
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
    vec4 cameraPos;
} ubo;

void main() {
    outUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(outUV * 2.0f - 1.0f, 0.0f, 1.0f);
}
