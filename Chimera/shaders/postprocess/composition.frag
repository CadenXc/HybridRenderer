#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common/common.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outFinalColor;

// --- [PHASE 1 DEBUG] Strictly matched to G-Buffer only ---
layout(set = 2, binding = 0) uniform sampler2D gAlbedo;   // Binding 0
layout(set = 2, binding = 1) uniform sampler2D gNormal;   // Binding 1
layout(set = 2, binding = 2) uniform sampler2D gMaterial; // Binding 2
layout(set = 2, binding = 3) uniform sampler2D gMotion;   // Binding 3
layout(set = 2, binding = 4) uniform sampler2D gDepth;    // Binding 4
layout(set = 2, binding = 5) uniform sampler2D gEmissive; // Binding 5

// --- [PHASE 3 SLOTS] RT/SVGF Signal Inputs ---
layout(set = 2, binding = 6) uniform sampler2D gGI_Raw;
layout(set = 2, binding = 7) uniform sampler2D gReflection_Raw;
layout(set = 2, binding = 8) uniform sampler2D gShadowAO_Raw;

void main() 
{
    float depth = texture(gDepth, inUV).r;
    
    // Explicitly unpack members from block-aligned UBO
    uint displayMode = frameData.z;
    uint renderFlags = frameData.w;
    float exposure   = postData.x;
    float ambStr     = postData.y;
    int skyIdx       = int(envData.x);

    // 1. 处理背景 (Environment)
    // Reversed-Z: 0.0 is far plane (skybox)
    if (depth <= 0.0001) 
    {
        if (displayMode == DISPLAY_MODE_NORMAL || displayMode == DISPLAY_MODE_MATERIAL)
        {
            outFinalColor = vec4(0.15, 0.15, 0.15, 1.0); 
            return;
        }
        
        if (skyIdx >= 0) 
        {
            // Reconstruction of direction from clip space
            vec4 clip = vec4(inUV * 2.0 - 1.0, 0.0, 1.0); // 0.0 is far in reversed-z
            vec4 view = camera.projInverse * clip;
            vec3 viewDir = normalize((camera.viewInverse * vec4(normalize(view.xyz), 0.0)).xyz);
            outFinalColor = vec4(texture(textureArray[nonuniformEXT(skyIdx)], SampleEquirectangular(viewDir)).rgb, 1.0);
        } 
        else 
        {
            outFinalColor = clearColor;
        }
        return;
    }

    // 2. 采样 G-Buffer 数据
    vec3 baseColor = texture(gAlbedo, inUV).rgb;
    vec3 emissive = texture(gEmissive, inUV).rgb;
    vec3 N_raw = texture(gNormal, inUV).xyz;
    vec3 worldNormal = length(N_raw) > 0.001 ? normalize(N_raw) : vec3(0, 1, 0);
    vec4 material = texture(gMaterial, inUV);
    float roughness = clamp(material.r, 0.05, 1.0); 
    float metallic = clamp(material.g, 0.0, 1.0);
    float ao = clamp(material.b, 0.0, 1.0);

    // 3. 调试视图切换
    if (displayMode == DISPLAY_MODE_ALBEDO) { outFinalColor = vec4(baseColor, 1.0); return; }
    if (displayMode == DISPLAY_MODE_NORMAL) { outFinalColor = vec4(worldNormal * 0.5 + 0.5, 1.0); return; }
    if (displayMode == DISPLAY_MODE_MATERIAL) { outFinalColor = vec4(roughness, metallic, ao, 1.0); return; }
    if (displayMode == DISPLAY_MODE_MOTION) { outFinalColor = vec4(abs(texture(gMotion, inUV).xy) * 10.0, 0.0, 1.0); return; }
    if (displayMode == DISPLAY_MODE_DEPTH) 
    { 
        // [FIX] Linearize depth for better debugging in Reversed-Z
        vec4 target = camera.projInverse * vec4(0.0, 0.0, depth, 1.0);
        float linearZ = abs(target.z / target.w);
        outFinalColor = vec4(vec3(exp(-linearZ * 0.1)), 1.0); // Fog-like visualization
        return; 
    }
    if (displayMode == DISPLAY_MODE_EMISSIVE) { outFinalColor = vec4(emissive, 1.0); return; }

    // 4. 核心物理合成
    vec3 worldPos = GetWorldPos(depth, inUV, camera.viewProjInverse);
    vec3 viewDirection = normalize(camera.position.xyz - worldPos);
    vec3 lightDirection = normalize(-sunLight.direction.xyz);
    vec3 lightIntensity = sunLight.color.rgb * sunLight.intensity.x;

    // 4. RT/SVGF Signals
    float shadowFactor = 1.0;
    bool hasRTShadow = (renderFlags & RENDER_FLAG_SHADOW_BIT) != 0;
    
    if (hasRTShadow)
    {
        shadowFactor = texture(gShadowAO_Raw, inUV).r;
    }
    else
    {
        // Screen space fallback or simple shadow mapping
        shadowFactor = 1.0; 
    }

    vec3 reflectionSignal = texture(gReflection_Raw, inUV).rgb;
    vec3 giSignal = texture(gGI_Raw, inUV).rgb;

    // 5. 调试视图切换 (Phase 3)
    if (displayMode == DISPLAY_MODE_SHADOW_AO) { outFinalColor = vec4(vec3(shadowFactor), 1.0); return; }
    if (displayMode == DISPLAY_MODE_REFLECTION) { outFinalColor = vec4(reflectionSignal, 1.0); return; }
    if (displayMode == DISPLAY_MODE_GI) { outFinalColor = vec4(giSignal, 1.0); return; }

    // 6. 核心物理合成
    // 6.1 直接光 PBR (Direct)
    vec3 directLighting = EvaluateDirectPBR(worldNormal, viewDirection, lightDirection, baseColor, roughness, metallic, lightIntensity) * shadowFactor;

    // 6.2 间接光 (Indirect / GI)
    vec3 F0 = mix(vec3(0.04), baseColor, metallic);
    vec3 fresnelTerm = FresnelSchlickRoughness(max(dot(worldNormal, viewDirection), 0.0), F0, roughness);
    
    // kS is the energy that reflects, kD is the energy that refracts/diffuses
    vec3 kS = fresnelTerm;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    
    // [FIX] GI signal should be modulated by albedo and refraction ratio (kD)
    vec3 indirectDiffuse = giSignal * baseColor * kD;
    
    // [FIX] Reflection signal from SVGF is already an irradiance/radiance sample
    // but needs to be weighted by the Fresnel term at the surface
    vec3 indirectSpecular = reflectionSignal * kS;

    // Fallback if no RT reflection/GI
    if ((renderFlags & RENDER_FLAG_GI_BIT) == 0) 
    {
        // Simple ambient with AO
        indirectDiffuse = ambStr * baseColor * ao * 0.05;
    }
    
    if ((renderFlags & RENDER_FLAG_REFLECTION_BIT) == 0 && skyIdx >= 0) 
    {
        vec3 reflectDirection = reflect(-viewDirection, worldNormal);
        vec3 envSpecular = texture(textureArray[nonuniformEXT(skyIdx)], SampleEquirectangular(reflectDirection)).rgb;
        indirectSpecular = envSpecular * kS * ambStr;
    }

    vec3 finalColor = directLighting + indirectDiffuse + indirectSpecular + emissive;

    finalColor *= exposure;
    finalColor = ACESToneMapping(finalColor);
    finalColor = pow(finalColor, vec3(1.0 / 2.2));

    outFinalColor = vec4(finalColor, 1.0);
}
