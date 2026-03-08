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
    uint displayMode = global.ubo.frameData.z;
    uint renderFlags = global.ubo.frameData.w;
    float exposure   = global.ubo.postData.x;
    float ambStr     = global.ubo.postData.y;
    int skyIdx       = int(global.ubo.envData.x);

    // 1. 处理背景 (Environment)
    if (depth == 0.0) 
    {
        if (displayMode == DISPLAY_MODE_NORMAL || displayMode == DISPLAY_MODE_MATERIAL)
        {
            outFinalColor = vec4(0.15, 0.15, 0.15, 1.0); 
            return;
        }
        
        if (skyIdx >= 0) 
        {
            vec3 worldPos = GetWorldPos(1.0, inUV, global.ubo.camera.viewProjInverse);
            vec3 viewDir = normalize(worldPos - global.ubo.camera.position.xyz);
            outFinalColor = vec4(texture(textureArray[nonuniformEXT(skyIdx)], SampleEquirectangular(viewDir)).rgb, 1.0);
        } 
        else 
        {
            outFinalColor = global.ubo.clearColor;
        }
        return;
    }

    // 2. 采样 G-Buffer 数据
    vec3 albedo = texture(gAlbedo, inUV).rgb;
    vec3 emissive = texture(gEmissive, inUV).rgb;
    vec3 N = normalize(texture(gNormal, inUV).xyz);
    vec4 material = texture(gMaterial, inUV);
    float roughness = max(material.r, 0.05); 
    float metallic = material.g;
    float ao = material.b;

    // 3. 调试视图切换
    if (displayMode == DISPLAY_MODE_ALBEDO) { outFinalColor = vec4(albedo, 1.0); return; }
    if (displayMode == DISPLAY_MODE_NORMAL) { outFinalColor = vec4(N * 0.5 + 0.5, 1.0); return; }
    if (displayMode == DISPLAY_MODE_MATERIAL) { outFinalColor = vec4(roughness, metallic, ao, 1.0); return; }
    if (displayMode == DISPLAY_MODE_MOTION) { outFinalColor = vec4(abs(texture(gMotion, inUV).xy) * 10.0, 0.0, 1.0); return; }
    if (displayMode == DISPLAY_MODE_DEPTH) { outFinalColor = vec4(vec3(depth), 1.0); return; }
    if (displayMode == DISPLAY_MODE_EMISSIVE) { outFinalColor = vec4(emissive, 1.0); return; }

    // 4. 核心物理合成
    vec3 worldPos = GetWorldPos(depth, inUV, global.ubo.camera.viewProjInverse);
    vec3 V = normalize(global.ubo.camera.position.xyz - worldPos);
    vec3 L = normalize(-global.ubo.sunLight.direction.xyz);
    vec3 lightIntensity = global.ubo.sunLight.color.rgb * global.ubo.sunLight.intensity.x;

    // 4. RT/SVGF Signals
    float shadowFactor = 1.0;
    bool hasRTShadow = (renderFlags & RENDER_FLAG_SHADOW_BIT) != 0;
    
    if (hasRTShadow)
    {
        shadowFactor = texture(gShadowAO_Raw, inUV).r;
    }
    else
    {
        vec3 ddx = dFdx(worldPos);
        vec3 ddy = dFdy(worldPos);
        vec3 faceNormal = normalize(cross(ddx, ddy));
        if (dot(faceNormal, V) < 0.0) faceNormal = -faceNormal;
        vec3 shadowOrigin = OffsetRay(worldPos, faceNormal);
        shadowFactor = CalculateRayQueryShadow(shadowOrigin, L, 1000.0);
    }

    vec3 reflectionSignal = texture(gReflection_Raw, inUV).rgb;
    vec3 giSignal = texture(gGI_Raw, inUV).rgb;

    // 5. 调试视图切换 (Phase 3)
    if (displayMode == DISPLAY_MODE_SHADOW_AO) { outFinalColor = vec4(vec3(shadowFactor), 1.0); return; }
    if (displayMode == DISPLAY_MODE_REFLECTION) { outFinalColor = vec4(reflectionSignal, 1.0); return; }
    if (displayMode == DISPLAY_MODE_GI) { outFinalColor = vec4(giSignal, 1.0); return; }

    // 6. 核心物理合成
    // 6.1 直接光 PBR
    vec3 directLighting = EvaluateDirectPBR(N, V, L, albedo, roughness, metallic, lightIntensity) * shadowFactor;

    // 6.2 间接光 (GI & Reflection)
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = F_SchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    
    vec3 indirectDiffuse = giSignal * albedo * kD;
    vec3 indirectSpecular = reflectionSignal * F;

    // Fallback if no RT reflection/GI
    if ((renderFlags & RENDER_FLAG_GI_BIT) == 0) 
    {
        indirectDiffuse = ambStr * albedo * ao * 0.1;
    }
    if ((renderFlags & RENDER_FLAG_REFLECTION_BIT) == 0 && skyIdx >= 0) 
    {
        vec3 reflectDir = reflect(-V, N);
        indirectSpecular = texture(textureArray[nonuniformEXT(skyIdx)], SampleEquirectangular(reflectDir)).rgb * F * ambStr;
    }

    vec3 finalColor = directLighting + indirectDiffuse + indirectSpecular + emissive;

    finalColor *= exposure;
    finalColor = ACESToneMapping(finalColor);
    finalColor = pow(finalColor, vec3(1.0 / 2.2));

    outFinalColor = vec4(finalColor, 1.0);
}
