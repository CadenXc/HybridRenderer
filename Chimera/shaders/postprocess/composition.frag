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
layout(set = 2, binding = 8) uniform sampler2D gShadow_Raw;
layout(set = 2, binding = 9) uniform sampler2D gAO_Raw;
layout(set = 2, binding = 10) uniform sampler2D gShadow_Debug_Raw; // NEW: Always un-denoised

void main() 
{
    float depth = texture(gDepth, inUV).r;
    
    // Explicitly unpack members from block-aligned UBO
    uint displayMode = frameData.z;
    uint renderFlags = frameData.w;
    float exposure   = postData.x;
    float ambStr     = postData.y;
    int skyIdx       = int(envData.x);
    vec4 finalClearColor = gpuClearColor;

    // 1. 处理背景 (Environment)
    if (depth <= 0.0001) 
    {
        if (displayMode == DISPLAY_MODE_NORMAL || displayMode == DISPLAY_MODE_MATERIAL)
        {
            outFinalColor = vec4(0.15, 0.15, 0.15, 1.0); 
            return;
        }
        
        // [FIX] Background rendering should respect IBL toggle
        bool hasIBL = (renderFlags & RENDER_FLAG_IBL_BIT) != 0;

        if (skyIdx >= 0 && hasIBL) 
        {
            // [FIX] Correct Y mapping for Vulkan: top is -1 in clip space
            vec2 clipUV = inUV * 2.0 - 1.0;
            vec4 clip = vec4(clipUV.x, clipUV.y, 0.0, 1.0); 
            vec4 view = camera.projInverse * clip;
            vec3 viewDir = normalize((camera.viewInverse * vec4(normalize(view.xyz), 0.0)).xyz);
            outFinalColor = vec4(texture(textureArray[nonuniformEXT(skyIdx)], SampleEquirectangular(viewDir)).rgb, 1.0);
        } 
        else 
        {
            // [PHYSICAL] No IBL means no sky light. It should be pitch black.
            outFinalColor = vec4(0.0, 0.0, 0.0, 1.0);
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
    float gBufferAO = clamp(material.b, 0.0, 1.0);

    // 3. 调试视图切换
    if (displayMode == DISPLAY_MODE_ALBEDO) { outFinalColor = vec4(baseColor, 1.0); return; }
    if (displayMode == DISPLAY_MODE_NORMAL) { outFinalColor = vec4(worldNormal * 0.5 + 0.5, 1.0); return; }
    if (displayMode == DISPLAY_MODE_MATERIAL) { outFinalColor = vec4(roughness, metallic, gBufferAO, 1.0); return; }
    if (displayMode == DISPLAY_MODE_MOTION) { outFinalColor = vec4(abs(texture(gMotion, inUV).xy) * 10.0, 0.0, 1.0); return; }
    if (displayMode == DISPLAY_MODE_DEPTH) 
    { 
        vec4 target = camera.projInverse * vec4(0.0, 0.0, depth, 1.0);
        float linearZ = abs(target.z / target.w);
        outFinalColor = vec4(vec3(linearZ * 0.01), 1.0); 
        return; 
    }
    if (displayMode == DISPLAY_MODE_EMISSIVE) { outFinalColor = vec4(emissive, 1.0); return; }

    // 4. 核心物理参数
    vec3 worldPos = GetWorldPos(depth, inUV, camera.viewProjInverse);
    vec3 viewDirection = normalize(camera.position.xyz - worldPos);
    vec3 lightDirection = normalize(-sunLight.direction.xyz);
    
    bool lightEnabled = (renderFlags & RENDER_FLAG_LIGHT_BIT) != 0;
    vec3 lightIntensity = lightEnabled ? (sunLight.color.rgb * sunLight.intensity.x) : vec3(0.0);

    // 4. RT/SVGF Signals
    float shadowFactor = 1.0;
    float rtAO = 1.0;
    bool hasRTShadow = (renderFlags & RENDER_FLAG_SHADOW_BIT) != 0;
    bool hasRTAO = (renderFlags & RENDER_FLAG_AO_BIT) != 0;
    bool hasIBL = (renderFlags & RENDER_FLAG_IBL_BIT) != 0;
    bool hasEmissive = (renderFlags & RENDER_FLAG_EMISSIVE_BIT) != 0;
    
    if (hasRTShadow)
    {
        shadowFactor = texture(gShadow_Raw, inUV).r;
    }
    
    if (hasRTAO)
    {
        rtAO = texture(gAO_Raw, inUV).r;
    }

    float shadowDebugFactor = texture(gShadow_Debug_Raw, inUV).r;
    vec3 reflectionSignal = texture(gReflection_Raw, inUV).rgb;
    vec3 giSignal = texture(gGI_Raw, inUV).rgb;

    // [ARCHITECTURAL DECISION] 
    // RT GI (Ray Traced Global Illumination) already accounts for physical occlusion
    // in crevices and corners (self-shadowing). Adding an extra AO pass on top of GI
    // can lead to double-darkening and breaks physical correctness.
    // We keep the AO pass for debug visualization, but it no longer affects the final composite.
    float finalAO = 1.0; // hasRTAO ? rtAO : gBufferAO; 

    // 5. 调试视图切换 - 这里强制使用未降噪的信号
    if (displayMode == DISPLAY_MODE_SHADOW) { outFinalColor = vec4(vec3(shadowDebugFactor), 1.0); return; }
    if (displayMode == DISPLAY_MODE_AO) { outFinalColor = vec4(vec3(finalAO), 1.0); return; }
    if (displayMode == DISPLAY_MODE_REFLECTION) { outFinalColor = vec4(reflectionSignal, 1.0); return; }
    if (displayMode == DISPLAY_MODE_GI) { outFinalColor = vec4(giSignal, 1.0); return; }

    // 6. 核心物理合成
    vec3 directLighting = EvaluateDirectPBR(worldNormal, viewDirection, lightDirection, baseColor, roughness, metallic, lightIntensity) * shadowFactor;

    vec3 F0 = mix(vec3(0.04), baseColor, metallic);
    vec3 fresnelTerm = FresnelSchlickRoughness(max(dot(worldNormal, viewDirection), 0.0), F0, roughness);
    
    vec3 kS = fresnelTerm;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    
    // [DEBUG AMPLIFICATION] Boost GI signal to make it visible during testing
    vec3 indirectDiffuse = giSignal * baseColor * kD * 5.0; 
    vec3 indirectSpecular = reflectionSignal * kS;

    if ((renderFlags & RENDER_FLAG_GI_BIT) == 0) 
    {
        indirectDiffuse = ambStr * baseColor * finalAO * 0.05;
    }
    
    // --- [DEBUG OVERRIDES] ---
    if (displayMode == 9) // DISPLAY_MODE_GI
    {
        // If we see pure magenta, it means the GI signal is being received but might be black
        if (length(giSignal) < 0.0001) outFinalColor = vec4(1.0, 0.0, 1.0, 1.0); 
        else outFinalColor = vec4(giSignal * 10.0, 1.0); 
        return;
    }
    
    // Apply IBL Toggle
    if (!hasIBL)
    {
        indirectSpecular = vec3(0.0);
        if ((renderFlags & RENDER_FLAG_GI_BIT) == 0) indirectDiffuse = vec3(0.0);
    }
    else if ((renderFlags & RENDER_FLAG_REFLECTION_BIT) == 0 && skyIdx >= 0) 
    {
        vec3 reflectDirection = reflect(-viewDirection, worldNormal);
        vec3 envSpecular = texture(textureArray[nonuniformEXT(skyIdx)], SampleEquirectangular(reflectDirection)).rgb;
        indirectSpecular = envSpecular * kS * ambStr * finalAO;
    }

    // --- [RESTORED PHYSICAL LIGHTING] ---
    vec3 finalEmissive = hasEmissive ? emissive : vec3(0.0); 
    vec3 finalColor = directLighting + indirectDiffuse + indirectSpecular + finalEmissive; 

    finalColor *= exposure;
    finalColor = pow(max(finalColor, vec3(0.0)), vec3(1.0 / 2.2));

    outFinalColor = vec4(finalColor, 1.0);
}
