#version 460
#include "ShaderCommon.h"

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform GlobalUBO 
{
    UniformBufferObject ubo;
} global;

layout(set = 2, binding = 0) uniform sampler2D gDepth;

void main() 
{
    float depth = texture(gDepth, inUV).r;
    
    // Background handling (Reverse-Z: depth 0 is infinity)
    if (depth <= 0.0001) 
    {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // 1. Linearize (Infinite Reverse-Z formula)
    // Z_world = Near / depth_buffer
    float n = 0.1; // Consistent with EditorCamera.cpp
    float linearZ = n / depth;
    
    // 2. Map to visualization range
    // We want to see detail in the 0.1m to 20m range
    // Vis = 1.0 (at near) -> 0.0 (at 20m)
    float vis = 1.0 - clamp((linearZ - n) / 20.0, 0.0, 1.0);
    
    // 3. Apply a slight gamma/power to enhance contrast in mid-range
    vis = pow(vis, 2.0);

    outColor = vec4(vec3(vis), 1.0);
}
