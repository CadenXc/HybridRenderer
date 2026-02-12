#version 460
#include "ShaderCommon.h"

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D gFinalColor;

void main() {
    outColor = texture(gFinalColor, inUV);
}
