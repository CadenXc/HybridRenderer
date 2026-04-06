#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common/common.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outFinalColor;

// --- G-Buffer Inputs ---
layout(set = 2, binding = 0) uniform sampler2D gAlbedo;   
layout(set = 2, binding = 1) uniform sampler2D gNormal;   
layout(set = 2, binding = 2) uniform sampler2D gMaterial; 
layout(set = 2, binding = 3) uniform sampler2D gMotion;   
layout(set = 2, binding = 4) uniform sampler2D gDepth;    
layout(set = 2, binding = 5) uniform sampler2D gEmissive; 

// --- RT/SVGF Signal Inputs ---
layout(set = 2, binding = 6) uniform sampler2D gGI;
layout(set = 2, binding = 7) uniform sampler2D gReflection;
layout(set = 2, binding = 8) uniform sampler2D gShadow; 
layout(set = 2, binding = 9) uniform sampler2D gAO;     
layout(set = 2, binding = 10) uniform sampler2D gShadow_Debug_Raw; 
layout(set = 2, binding = 11) uniform sampler2D gShadow_Moments;   

void main() 
{
    float depth = texture(gDepth, inUV).r;
    uint displayMode = frameData.z;
    uint renderFlags = frameData.w;
    float exposure   = postData.x;
    float ambStr     = postData.y;
    int skyIdx       = int(envData.x);

    if (depth <= 0.0001) 
    {
        if (displayMode == DISPLAY_MODE_NORMAL || displayMode == DISPLAY_MODE_MATERIAL) { outFinalColor = vec4(0.15, 0.15, 0.15, 1.0); return; }
        bool hasIBL = (renderFlags & RENDER_FLAG_IBL_BIT) != 0;
        if (skyIdx >= 0 && hasIBL) {
            vec2 clipUV = inUV * 2.0 - 1.0;
            vec4 view = camera.projInverse * vec4(clipUV.x, clipUV.y, 0.0, 1.0); 
            vec3 viewDir = normalize((camera.viewInverse * vec4(normalize(view.xyz), 0.0)).xyz);
            outFinalColor = vec4(texture(textureArray[nonuniformEXT(skyIdx)], SampleEquirectangular(viewDir)).rgb, 1.0);
        } else {
            outFinalColor = vec4(0.0, 0.0, 0.0, 1.0);
        }
        return;
    }

    vec3 baseColor = texture(gAlbedo, inUV).rgb;
    vec3 emissive = texture(gEmissive, inUV).rgb;
    vec3 worldNormal = normalize(texture(gNormal, inUV).xyz);
    vec4 material = texture(gMaterial, inUV);
    float roughness = clamp(material.r, 0.05, 1.0); 
    float metallic = clamp(material.g, 0.0, 1.0);
    float gBufferAO = clamp(material.b, 0.0, 1.0);

    // --- 1. Indirect Signals (Filtered Irradiance) ---
    vec4 shadowAO = texture(gShadow, inUV);
    float shadowFactor = shadowAO.r;
    float rtAO = shadowAO.g; 

    // Both GI and Reflection were demodulated by gAlbedo in SVGF, 
    // so they are now Irradiance (Incoming light field).
    vec3 giIrradiance = texture(gGI, inUV).rgb;
    vec3 reflIrradiance = texture(gReflection, inUV).rgb;

    if (displayMode == DISPLAY_MODE_ALBEDO) { outFinalColor = vec4(baseColor, 1.0); return; }
    if (displayMode == DISPLAY_MODE_NORMAL) { outFinalColor = vec4(worldNormal * 0.5 + 0.5, 1.0); return; }
    if (displayMode == DISPLAY_MODE_SHADOW) { outFinalColor = vec4(vec3(shadowFactor), 1.0); return; }
    if (displayMode == DISPLAY_MODE_AO) { outFinalColor = vec4(vec3(rtAO), 1.0); return; }
    if (displayMode == DISPLAY_MODE_GI) { outFinalColor = vec4(giIrradiance, 1.0); return; }
    if (displayMode == DISPLAY_MODE_REFLECTION) { outFinalColor = vec4(reflIrradiance, 1.0); return; }

    // --- 2. Physical Composition ---
    vec3 worldPos = GetWorldPos(depth, inUV, camera.viewProjInverse);
    vec3 viewDir = normalize(camera.position.xyz - worldPos);
    vec3 lightDir = normalize(-sunLight.direction.xyz);
    vec3 lightIntensity = (renderFlags & RENDER_FLAG_LIGHT_BIT) != 0 ? (sunLight.color.rgb * sunLight.intensity.x) : vec3(0.0);

    // A. Direct Lighting (Radiance)
    vec3 directRadiance = EvaluateDirectPBR(worldNormal, viewDir, lightDir, baseColor, roughness, metallic, lightIntensity) * shadowFactor;

    // B. Indirect Diffuse (GI)
    // Physically Correct: GI_Irradiance * Diffuse_Albedo
    // Diffuse_Albedo = baseColor * (1.0 - metallic)
    vec3 F0 = mix(vec3(0.04), baseColor, metallic);
    vec3 F = FresnelSchlickRoughness(max(dot(worldNormal, viewDir), 0.0), F0, roughness);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    
    vec3 indirectDiffuse = giIrradiance * baseColor * kD; 

    // C. Indirect Specular (Reflection)
    // Physically Correct: Specular_Irradiance * Fresnel
    vec3 indirectSpecular = reflIrradiance * F;

    // D. IBL Fallback (if RT GI is off)
    if ((renderFlags & RENDER_FLAG_GI_BIT) == 0) {
        // Simple ambient fallback using RTAO
        indirectDiffuse = ambStr * baseColor * rtAO * 0.1;
    }
    
    if ((renderFlags & RENDER_FLAG_REFLECTION_BIT) == 0 && (renderFlags & RENDER_FLAG_IBL_BIT) == 0) {
        indirectSpecular = vec3(0.0);
    }

    // --- 3. Final Sum ---
    vec3 finalColor = directRadiance + indirectDiffuse + indirectSpecular + (emissive * ((renderFlags & RENDER_FLAG_EMISSIVE_BIT) != 0 ? 1.0 : 0.0)); 

    finalColor *= exposure;
    finalColor = pow(max(finalColor, vec3(0.0)), vec3(1.0 / 2.2));

    outFinalColor = vec4(finalColor, 1.0);
}
