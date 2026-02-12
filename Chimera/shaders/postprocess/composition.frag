#version 460
#include "ShaderCommon.h"

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outFinalColor;

layout(set = 2, binding = 0) uniform sampler2D gAlbedo;
layout(set = 2, binding = 1) uniform sampler2D gShadowAO;

void main() {
    vec3 albedo = texture(gAlbedo, inUV).rgb;
    outFinalColor = vec4(albedo, 1.0);
}
