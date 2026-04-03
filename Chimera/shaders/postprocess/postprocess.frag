#version 460
#include "ShaderCommon.h"

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D inColor;

void main() 
{
    // [DEBUG] Bypass jitter stabilization, but keep basic color processing
    vec3 color = texture(inColor, inUV).rgb;

    // 1. Exposure
    color *= postData.x;

    // 2. Gamma Correction (Essential for visibility)
    color = pow(max(color, vec3(0.0)), vec3(1.0/2.2));

    outColor = vec4(color, 1.0);
}
