#version 450
#extension GL_GOOGLE_include_directive : require
#include "pbr.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

struct DirectionalLight
{
    mat4 projview;
    vec4 direction;
    vec4 color;
    vec4 intensity;
};

layout(set = 0, binding = 0) uniform CameraProperties {
    mat4 view;
    mat4 proj;
    mat4 viewInverse;
    mat4 projInverse;
    mat4 viewProjInverse;
    mat4 prevView;
    mat4 prevProj;
    DirectionalLight directionalLight;
    vec2 displaySize;
    vec2 displaySizeInverse;
    uint frameIndex;
    uint frameCount;
    uint displayMode;
    vec4 cameraPos;
} cam;

layout(set = 1, binding = 0) uniform sampler2D samplerAlbedo;
layout(set = 1, binding = 1) uniform sampler2D samplerNormal;
layout(set = 1, binding = 2) uniform sampler2D samplerMaterial; // x: roughness, y: metallic
layout(set = 1, binding = 3) uniform sampler2D samplerDepth;
layout(set = 1, binding = 4) uniform sampler2D samplerShadowAO;
layout(set = 1, binding = 5) uniform sampler2D samplerReflection;

void main() {
    vec3 albedo = texture(samplerAlbedo, inUV).rgb;
    vec3 normal = texture(samplerNormal, inUV).rgb * 2.0 - 1.0;
    vec4 matParams = texture(samplerMaterial, inUV);
    float roughness = matParams.r;
    float metallic = matParams.g;
    float depth = texture(samplerDepth, inUV).r;

    if (depth == 1.0) {
        outColor = vec4(albedo * 0.05, 1.0); // Background
        return;
    }

    // Reconstruct World Position
    vec4 clipPos = vec4(inUV * 2.0 - 1.0, depth, 1.0);
    vec4 worldPos = cam.viewProjInverse * clipPos;
    worldPos /= worldPos.w;

    vec3 V = normalize(cam.cameraPos.xyz - worldPos.xyz);
    vec3 N = normalize(normal);
    vec3 L = normalize(-cam.directionalLight.direction.xyz);
    
    // Read Raytraced Shadow, AO and Reflections
    vec2 shadowAO = texture(samplerShadowAO, inUV).rg;
    
    // Fallback if RT not supported (texture might contain 0 or placeholder data)
    if (cam.displayMode == 0 && shadowAO.x == 0.0 && shadowAO.y == 0.0) {
        shadowAO = vec2(1.0, 1.0); 
    }

    float shadow = shadowAO.r;
    float ao = shadowAO.g;
    vec3 reflection = texture(samplerReflection, inUV).rgb;

    // Debug Modes
    if (cam.displayMode == 1) { outColor = vec4(vec3(shadow), 1.0); return; }
    if (cam.displayMode == 2) { outColor = vec4(vec3(ao), 1.0); return; }
    if (cam.displayMode == 3) { outColor = vec4(reflection, 1.0); return; }

    vec3 radiance = cam.directionalLight.color.rgb * cam.directionalLight.intensity.x * shadow;
    
    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo, metallic);

    vec3 directLighting = CalculatePBR(N, V, L, albedo, roughness, metallic, F0, radiance);

    // Ambient modulated by AO
    vec3 ambient = albedo * 0.03 * ao;

    // Specular Reflection blending
    vec3 F = FresnelSchlick(max(dot(N, V), 0.0), F0);
    vec3 specularReflection = reflection * F * (1.0 - roughness);

    vec3 color = directLighting + ambient + specularReflection;

    // Tone Mapping (Reinhard)
    color = color / (color + vec3(1.0));
    // Gamma Correction
    color = pow(color, vec3(1.0/2.2));

    outColor = vec4(color, 1.0);
}