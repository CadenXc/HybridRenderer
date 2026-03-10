#version 460
#include "ShaderCommon.h"

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D gDepth;

void main() 
{
    float depth = texture(gDepth, inUV).r;
    
    if (depth <= 0.0001) 
    {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float n = 0.1; // Consistent with EditorCamera.cpp
    float linearZ = n / depth;
    
    float vis = 1.0 - clamp((linearZ - n) / 20.0, 0.0, 1.0);
    
    vis = pow(vis, 2.0);

    outColor = vec4(vec3(vis), 1.0);
}
