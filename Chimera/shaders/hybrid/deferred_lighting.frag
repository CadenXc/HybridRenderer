#version 450
#extension GL_GOOGLE_include_directive : require
#include "ShaderCommon.h"
#include "pbr.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform GlobalUBO 
{
    UniformBufferObject cam;
};

// Set 2: G-Buffer & Lighting Resources
layout(set = 2, binding = 0) uniform sampler2D Albedo;
layout(set = 2, binding = 1) uniform sampler2D Normal;
layout(set = 2, binding = 2) uniform sampler2D Material;
layout(set = 2, binding = 3) uniform sampler2D Depth;
layout(set = 2, binding = 4) uniform sampler2D ShadowAO;
layout(set = 2, binding = 5) uniform sampler2D Reflections;

void main() 
{
    float depth = texture(Depth, inUV).r;

    // Background handling (Infinite Reverse-Z: far is 0.0)
    if (depth <= 0.00001) 
    {
        outColor = vec4(0.02, 0.02, 0.03, 1.0); // Simple dark space
        return;
    }

    // 1. Reconstruct World Position
    vec3 worldPos = GetWorldPos(depth, inUV, cam.viewProjInverse);

    // 2. Fetch G-Buffer Data & Apply SRGB conversion (since we use UNORM textures)
    vec3 baseColor = pow(texture(Albedo, inUV).rgb, vec3(2.2));
    vec3 normal = texture(Normal, inUV).rgb; 
    vec4 matParams = texture(Material, inUV);
    
    float roughness = matParams.r;
    float metallic = matParams.g;
    float ao_from_map = matParams.b;

    // 3. Lighting Vectors
    vec3 N = normalize(normal);
    vec3 V = normalize(cam.cameraPos.xyz - worldPos);
    vec3 L = normalize(-cam.directionalLight.direction.xyz);
    
    // 4. Real Shadow & AO from Ray Tracing Pass
    vec2 shadowAO = texture(ShadowAO, inUV).rg;
    float shadow = shadowAO.r;
    float ao = shadowAO.g * ao_from_map; 
    
    // 5. Real Reflection from Ray Tracing Pass
    vec3 reflection = pow(texture(Reflections, inUV).rgb, vec3(2.2));

    // 6. PBR Calculation
    vec3 radiance = cam.directionalLight.color.rgb * cam.directionalLight.intensity.x * shadow;
    vec3 F0 = mix(vec3(0.04), baseColor, metallic);
    
    vec3 directLighting = CalculatePBR(N, V, L, baseColor, roughness, metallic, F0, radiance);
    
    // 7. Ambient Term
    vec3 ambient = baseColor * 0.03 * ao;

    // 8. Combine with Reflection
    // We mask reflection based on Fresnel and Roughness for a more physical look
    vec3 F = FresnelSchlick(max(dot(N, V), 0.0), F0);
    vec3 kS = F;
    vec3 specularReflection = reflection * kS * (1.0 - roughness);

    vec3 color = directLighting + ambient + specularReflection;

    // 9. Tone Mapping & Gamma Correction
    color = color / (color + vec3(1.0)); // Simple Reinhard
    color = pow(color, vec3(1.0/2.2));
    
    outColor = vec4(color, 1.0);
}
