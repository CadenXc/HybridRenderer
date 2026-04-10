#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common/common.glsl"

/**
 * @file miss.rmiss
 * @brief Ray Tracing Miss Shader for Radiance (Primary/Indirect)
 * 
 * This shader is triggered when a radiance ray (e.g., GI, Reflections, AO) does NOT hit 
 * any geometry in the scene. In such cases:
 * - Radiance is fetched from the environment skybox (if IBL is enabled).
 * - A procedural sky gradient is used as a fallback if no skybox is provided.
 * - Distance is set to -1.0 to signal that no geometry was encountered.
 * 
 * Note: For pure Shadow rays, shadow.rmiss is typically used instead.
 */

layout(location = 0) rayPayloadInEXT HitPayload payload;

void main() 
{
    vec3 skyColor = vec3(0.0);
    int skyIdx = int(envData.x);
    
    // Check if Image-Based Lighting (IBL) is enabled in the current render pass.
    bool hasIBL = (uint(frameData.w) & RENDER_FLAG_IBL_BIT) != 0;

    if (hasIBL)
    {
        if (skyIdx >= 0)
        {
            // Sample the environment map using equirectangular mapping.
            skyColor = texture(textureArray[nonuniformEXT(skyIdx)], SampleEquirectangular(gl_WorldRayDirectionEXT)).rgb;
        }
        else
        {
            // Fallback: Procedural sky gradient based on ray direction.
            // Horizon is lighter, zenith is darker.
            float t = 0.5 * (gl_WorldRayDirectionEXT.y + 1.0);
            skyColor = mix(vec3(0.4, 0.5, 0.6), vec3(0.1, 0.2, 0.4), t); 
            
            // Artificial Sun for visual reference.
            float sun = pow(max(0.0, dot(gl_WorldRayDirectionEXT, normalize(vec3(1.0, 1.0, -1.0)))), 128.0);
            skyColor += vec3(sun * 5.0);
        }
    }
    else
    {
        // If IBL is disabled, we assume a physically dark environment (e.g. night or enclosed space).
        skyColor = vec3(0.0);
    }

    // color_dist.w = -1.0 indicates a Miss (no hit distance).
    payload.color_dist = vec4(skyColor, -1.0);
    // motion_hit.b = 0.0 indicates No Occlusion (Visibility=1.0).
    payload.motion_hit.b = 0.0;
}
