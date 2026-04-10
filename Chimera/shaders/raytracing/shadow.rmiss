#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common/common.glsl"

/**
 * @file shadow.rmiss
 * @brief Ray Tracing Miss Shader for Shadows
 * 
 * This shader is triggered when a shadow ray (cast via traceRayEXT) does NOT hit 
 * any geometry in the scene. In a standard shadow visibility test:
 * - Miss = Visible (No occluder found between origin and light)
 * - Hit = Occluded (Geometry found blocking the light)
 * 
 * Note: If using Hardware Ray Query (Inline Ray Tracing), this shader is bypassed 
 * as the visibility is determined directly within the ray query loop.
 */

layout(location = 0) rayPayloadInEXT HitPayload payload;

void main() 
{
    // Visibility logic:
    // If we trigger a Miss, the path is clear.
    // We set the hit flag in the payload to 0.0 (No Hit -> Visible).
    payload.motion_hit.b = 0.0;
}
