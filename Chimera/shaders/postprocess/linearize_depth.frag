#version 460
#include "ShaderCommon.h"

layout(location = 0) in vec2 inUV;
layout(location = 0) out float outLinearDepth;

layout(set = 0, binding = 0) uniform GlobalUBO 
{
    UniformBufferObject ubo;
} global;

layout(set = 2, binding = 0) uniform sampler2D gDepth;

void main() 
{
    float depth = texture(gDepth, inUV).r;
    
    // Reverse-Z 线性化: Z = near / depth
    float n = 0.1;
    
    if (depth <= 0.00001) 
    {
        outLinearDepth = 0.0;
        return;
    }

    float linearZ = n / depth;
    
    // 可视化：将 0-50米 映射到 0-1 范围
    outLinearDepth = clamp(linearZ / 50.0, 0.0, 1.0);
}
