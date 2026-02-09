#version 450
#extension GL_GOOGLE_include_directive : require
#include "ShaderCommon.h"

layout(location = 0) out vec2 outUV;

layout(set = 0, binding = 0) uniform GlobalUBO {
    UniformBufferObject cam;
} ubo;

void main() {
    outUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(outUV * 2.0f - 1.0f, 0.0f, 1.0f);
}