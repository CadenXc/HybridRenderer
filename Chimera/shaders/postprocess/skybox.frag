#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common/common.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

void main() 
{
    // Sample from Depth Buffer to only draw on empty pixels
    // Reversed-Z: 0.0 is background
    
    int skyIdx = int(envData.x);
    if (skyIdx >= 0) 
    {
        // Reconstruct direction from UV assuming it's on far plane (0.0 in reversed-z)
        vec4 clip = vec4(inUV * 2.0 - 1.0, 0.0, 1.0);
        vec4 view = camera.projInverse * clip;
        vec3 viewDir = normalize((camera.viewInverse * vec4(normalize(view.xyz), 0.0)).xyz);
        outColor = vec4(texture(textureArray[nonuniformEXT(skyIdx)], SampleEquirectangular(viewDir)).rgb, 1.0);
    } 
    else 
    {
        outColor = gpuClearColor;
    }
}
