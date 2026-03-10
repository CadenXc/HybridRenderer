#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common/common.glsl"

layout(location = 0) rayPayloadInEXT HitPayload payload;

void main() 
{
    vec3 skyColor = vec3(0.0);
    int skyIdx = int(envData.x);
    if (skyIdx >= 0)
    {
        skyColor = texture(textureArray[nonuniformEXT(skyIdx)], SampleEquirectangular(gl_WorldRayDirectionEXT)).rgb;
    }
    else
    {
        // [FALLBACK] Procedural procedural sky gradient if HDRI is missing
        // A nice blue-to-white gradient based on Y-up direction
        float t = 0.5 * (gl_WorldRayDirectionEXT.y + 1.0);
        skyColor = mix(vec3(0.4, 0.5, 0.6), vec3(0.1, 0.2, 0.4), t); 
        
        // Add a fake "Sun" dot for reflections
        float sun = pow(max(0.0, dot(gl_WorldRayDirectionEXT, normalize(vec3(1.0, 1.0, -1.0)))), 128.0);
        skyColor += vec3(sun * 5.0);
    }

    payload.color_dist = vec4(skyColor, -1.0);
    payload.motion_hit.b = 0.0;
}
