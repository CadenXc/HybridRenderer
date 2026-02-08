#version 450
#extension GL_GOOGLE_include_directive : require
#include "pbr.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

struct DirectionalLight { mat4 projview; vec4 direction; vec4 color; vec4 intensity; };
layout(set = 0, binding = 0) uniform CameraProperties {
    mat4 view; mat4 proj; mat4 viewInverse; mat4 projInverse; mat4 viewProjInverse;
    mat4 prevView; mat4 prevProj; DirectionalLight directionalLight;
    vec2 displaySize; vec2 displaySizeInverse; uint frameIndex; uint frameCount; uint displayMode; vec4 cameraPos;
} cam;

// --- 按契约重命名 ---
layout(set = 1, binding = 0) uniform sampler2D Albedo;
layout(set = 1, binding = 1) uniform sampler2D Normal;
layout(set = 1, binding = 2) uniform sampler2D Material;
layout(set = 1, binding = 3) uniform sampler2D Depth;
layout(set = 1, binding = 4) uniform sampler2D ShadowAO;
layout(set = 1, binding = 5) uniform sampler2D Reflections;

void main() {
    vec3 albedo = texture(Albedo, inUV).rgb;
    vec3 normal = texture(Normal, inUV).rgb * 2.0 - 1.0;
    vec4 matParams = texture(Material, inUV);
    float roughness = matParams.r;
    float metallic = matParams.g;
    float depth = texture(Depth, inUV).r;

    if (depth == 1.0) {
        outColor = vec4(albedo * 0.05, 1.0);
        return;
    }

    vec4 clipPos = vec4(inUV * 2.0 - 1.0, depth, 1.0);
    vec4 worldPos = cam.viewProjInverse * clipPos;
    worldPos /= worldPos.w;

    vec3 V = normalize(cam.cameraPos.xyz - worldPos.xyz);
    vec3 N = normalize(normal);
    vec3 L = normalize(-cam.directionalLight.direction.xyz);
    
    vec2 shadowAO = texture(ShadowAO, inUV).rg;
    float shadow = shadowAO.r;
    float ao = shadowAO.g;
    vec3 reflection = texture(Reflections, inUV).rgb;

    // Debug Modes
    if (cam.displayMode == 1) { outColor = vec4(vec3(shadow), 1.0); return; }
    if (cam.displayMode == 2) { outColor = vec4(vec3(ao), 1.0); return; }
    if (cam.displayMode == 3) { outColor = vec4(reflection, 1.0); return; }

    vec3 radiance = cam.directionalLight.color.rgb * cam.directionalLight.intensity.x * shadow;
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 directLighting = CalculatePBR(N, V, L, albedo, roughness, metallic, F0, radiance);
    vec3 ambient = albedo * 0.03 * ao;
    vec3 F = FresnelSchlick(max(dot(N, V), 0.0), F0);
    vec3 specularReflection = reflection * F * (1.0 - roughness);

    vec3 color = directLighting + ambient + specularReflection;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));
    outColor = vec4(color, 1.0);
}
