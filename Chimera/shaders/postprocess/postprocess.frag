#version 460
#include "ShaderCommon.h"

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D inColor;

void main() 
{
    // [PHASE 5: Optical Image Stabilization]
    // TAA output is jittered to preserve raw sharpness. 
    // We stabilize it here using the final bilinear sample.
    vec2 jitterUV = camera.jitterData.xy * 0.5;
    vec3 color = texture(inColor, inUV + jitterUV).rgb;

    // 1. Exposure
    vec3 finalHDR = color * postData.x;

    // 2. Tone Mapping (ACES Filmic Approximation)
    vec3 x = finalHDR;
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    vec3 finalSDR = clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);

    // 3. Gamma Correction
    finalSDR = pow(finalSDR, vec3(1.0/2.2));

    outColor = vec4(finalSDR, 1.0);
}
