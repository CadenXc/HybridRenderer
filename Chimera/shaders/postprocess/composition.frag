#version 460
#extension GL_EXT_nonuniform_qualifier : enable
#include "ShaderCommon.h"
#include "../common/pbr.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outFinalColor;

layout(set = 0, binding = 0) uniform GlobalUBO 
{
    UniformBufferObject ubo;
} global;

layout(set = 2, binding = 0) uniform sampler2D gAlbedo;
layout(set = 2, binding = 1) uniform sampler2D gShadowAO_Filtered;
layout(set = 2, binding = 2) uniform sampler2D gReflection_Filtered;
layout(set = 2, binding = 3) uniform sampler2D gGI_Filtered;
layout(set = 2, binding = 4) uniform sampler2D gMaterial;
layout(set = 2, binding = 5) uniform sampler2D gNormal;
layout(set = 2, binding = 6) uniform sampler2D gDepth;
layout(set = 2, binding = 7) uniform sampler2D gShadowAO_Raw;
layout(set = 2, binding = 8) uniform sampler2D gReflection_Raw;
layout(set = 2, binding = 9) uniform sampler2D gGI_Raw;
layout(set = 2, binding = 10) uniform sampler2D gMotion;

// --- IBL Inputs ---
layout(set = 1, binding = 3) uniform sampler2D textureArray[];

layout(push_constant) uniform PushConstants 
{
    int skyboxIndex;
} pc;

vec2 SampleEquirectangular(vec3 v) 
{
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= vec2(0.1591, 0.3183); 
    uv += 0.5;
    return uv;
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) 
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ACES fitted
vec3 ACESToneMapping(vec3 x) 
{
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() 
{
    float depth = texture(gDepth, inUV).r;
    
    if (depth == 0.0) 
    {
        if (pc.skyboxIndex >= 0) 
        {
            vec3 worldPos = GetWorldPos(1.0, inUV, global.ubo.camera.viewProjInverse);
            vec3 viewDir = normalize(worldPos - global.ubo.camera.position.xyz);
            outFinalColor = vec4(texture(textureArray[nonuniformEXT(pc.skyboxIndex)], SampleEquirectangular(viewDir)).rgb, 1.0);
        } 
        else 
        {
            outFinalColor = vec4(0.05, 0.05, 0.07, 1.0);
        }
        return;
    }

    bool denoisingEnabled = (global.ubo.renderFlags & RENDER_FLAG_SVGF_BIT) != 0;
    
    vec3 albedo = texture(gAlbedo, inUV).rgb;
    // [FIX] Decode normals from [0, 1] to [-1, 1]
    vec3 N = normalize(texture(gNormal, inUV).xyz * 2.0 - 1.0);
    vec4 material = texture(gMaterial, inUV);
    
    // Standardized: R = Roughness, G = Metallic
    float roughness = max(material.r, 0.05); 
    float metallic = material.g;
    
    float shadowAO = denoisingEnabled ? texture(gShadowAO_Filtered, inUV).r : texture(gShadowAO_Raw, inUV).r;
    vec3 reflection = denoisingEnabled ? texture(gReflection_Filtered, inUV).rgb : texture(gReflection_Raw, inUV).rgb;
    vec3 gi = denoisingEnabled ? texture(gGI_Filtered, inUV).rgb : texture(gGI_Raw, inUV).rgb;

    // Debug Modes (Using Refactored Macros)
    if (global.ubo.displayMode == DISPLAY_MODE_ALBEDO) { outFinalColor = vec4(albedo, 1.0); return; }
    if (global.ubo.displayMode == DISPLAY_MODE_NORMAL) { outFinalColor = vec4(N * 0.5 + 0.5, 1.0); return; }
    if (global.ubo.displayMode == DISPLAY_MODE_MATERIAL) { outFinalColor = vec4(roughness, metallic, 0.0, 1.0); return; }
    if (global.ubo.displayMode == DISPLAY_MODE_DEPTH) { outFinalColor = vec4(vec3(depth), 1.0); return; }
    if (global.ubo.displayMode == DISPLAY_MODE_SHADOW_AO) { outFinalColor = vec4(vec3(shadowAO), 1.0); return; }
    if (global.ubo.displayMode == DISPLAY_MODE_REFLECTION) { outFinalColor = vec4(reflection, 1.0); return; }
    // Note: 8 was previously GI, adding macro if needed or keeping as logic
    if (global.ubo.displayMode == 8) { outFinalColor = vec4(gi, 1.0); return; }

    vec3 worldPos = GetWorldPos(depth, inUV, global.ubo.camera.viewProjInverse);
    vec3 V = normalize(global.ubo.camera.position.xyz - worldPos);

    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo, metallic);

    // 1. Direct Lighting (Directional Light)
    vec3 L = normalize(-global.ubo.sunLight.direction.xyz);
    vec3 lightColor = global.ubo.sunLight.color.rgb * global.ubo.sunLight.intensity.x;
    vec3 directLighting = CalculatePBR(N, V, L, albedo, roughness, metallic, F0, lightColor) * shadowAO;

    // 2. Indirect Lighting (Ambient)
    // Diffuse Indirect (GI)
    float cosTheta = max(dot(N, V), 0.0);
    vec3 F_gi = fresnelSchlickRoughness(cosTheta, F0, roughness);
    vec3 kS_gi = F_gi;
    vec3 kD_gi = (1.0 - kS_gi) * (1.0 - metallic);
    
    // Weighted Indirect
    vec3 ambientDiffuse = gi * kD_gi * global.ubo.ambientStrength;
    vec3 ambientSpecular = reflection * kS_gi * global.ubo.ambientStrength;

    vec3 ambient = (ambientDiffuse + ambientSpecular) * shadowAO;

    // 3. Final Composition
    vec3 finalColor = directLighting + ambient;
    
    // 4. Output Raw HDR color (Post-Process handled in TAA)
    outFinalColor = vec4(finalColor, 1.0);
}
