#version 460
#include "ShaderCommon.h"

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D inColor;

vec3 ToneMapACES(vec3 color)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;

    return clamp(
        (color * (a * color + b)) /
        (color * (c * color + d) + e),
        0.0,
        1.0);
}

vec3 LinearToSrgb(vec3 linearColor)
{
    vec3 low = linearColor * 12.92;
    vec3 high = 1.055 * pow(linearColor, vec3(1.0 / 2.4)) - 0.055;
    return mix(low, high, step(vec3(0.0031308), linearColor));
}

void main() 
{
    vec3 color = max(texture(inColor, inUV).rgb, vec3(0.0));
    color *= postData.x;
    color = ToneMapACES(color);
    if ((frameData.w & RENDER_FLAG_MANUAL_OUTPUT_SRGB_BIT) != 0)
    {
        color = LinearToSrgb(color);
    }

    outColor = vec4(color, 1.0);
}
