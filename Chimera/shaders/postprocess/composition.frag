#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common/common.glsl"

/**
 * @file composition.frag
 * @brief 最终合成片元着色器
 * 
 * 主要职责：
 * 1. 从 G-Buffer 读入几何与材质参数。
 * 2. 结合 RT/SVGF 输出的阴影、反射、间接光信号。
 * 3. 执行基于物理的着色（PBR Composition）。
 * 4. 应用 Tone Mapping 和伽马校正输出至 SDR 空间。
 */

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outFinalColor;

// --- 绑定顺序与 CompositionPass::Setup 对应 ---
layout(set = 2, binding = 0) uniform sampler2D gAlbedo;   
layout(set = 2, binding = 1) uniform sampler2D gNormal;   
layout(set = 2, binding = 2) uniform sampler2D gMaterialParams; 
layout(set = 2, binding = 3) uniform sampler2D gMotion;   
layout(set = 2, binding = 4) uniform sampler2D gDepth;    
layout(set = 2, binding = 5) uniform sampler2D gEmissive; 

layout(set = 2, binding = 6) uniform sampler2D gGI;
layout(set = 2, binding = 7) uniform sampler2D gReflection;
layout(set = 2, binding = 8) uniform sampler2D gShadow; 
layout(set = 2, binding = 9) uniform sampler2D gAO;     

void main() 
{
    // --- 1. 获取全局参数与深度控制 ---
    float depth = texture(gDepth, inUV).r;
    uint displayMode = frameData.z;
    uint renderFlags = frameData.w;
    float exposure   = postData.x;
    float ambStr     = postData.y;
    int skyIdx       = int(envData.x);

    // 背景处理：天空盒绘制
    if (depth <= 0.0001) 
    {
        if (displayMode == DISPLAY_MODE_NORMAL || displayMode == DISPLAY_MODE_MATERIAL) { 
            outFinalColor = vec4(0.15, 0.15, 0.15, 1.0); 
            return; 
        }
        bool hasIBL = (renderFlags & RENDER_FLAG_IBL_BIT) != 0;
        if (skyIdx >= 0 && hasIBL) {
            vec3 viewDir = normalize((camera.viewInverse * camera.projInverse * vec4(inUV * 2.0 - 1.0, 0.0, 1.0)).xyz);
            outFinalColor = vec4(texture(textureArray[nonuniformEXT(skyIdx)], SampleEquirectangular(viewDir)).rgb, 1.0);
        } else {
            outFinalColor = vec4(0.0, 0.0, 0.0, 1.0);
        }
        return;
    }

    // --- 2. 提取 G-Buffer 物理属性 ---
    vec3 baseColor = texture(gAlbedo, inUV).rgb;
    vec3 emissive = texture(gEmissive, inUV).rgb;
    vec3 worldNormal = normalize(texture(gNormal, inUV).xyz);
    
    vec4 matParams = texture(gMaterialParams, inUV);
    float roughness = matParams.r; 
    float metallic = matParams.g;
    float gBufferAO = matParams.b;
    int materialType = int(matParams.a * 255.0 + 0.5);

    // --- 3. 获取光线追踪/降噪后的间接信号 ---
    vec4 shadowAO = texture(gShadow, inUV);
    float shadowFactor = shadowAO.r; // 光追软阴影
    float rtAO = shadowAO.g;         // 光追环境遮蔽

    vec3 giIrradiance = texture(gGI, inUV).rgb;
    vec3 reflRadiance = texture(gReflection, inUV).rgb;

    // --- 4. 调试模式分支 ---
    if (displayMode == DISPLAY_MODE_ALBEDO) { outFinalColor = vec4(baseColor, 1.0); return; }
    if (displayMode == DISPLAY_MODE_NORMAL) { outFinalColor = vec4(worldNormal * 0.5 + 0.5, 1.0); return; }
    if (displayMode == DISPLAY_MODE_MATERIAL) { outFinalColor = vec4(matParams.rgb, 1.0); return; }
    if (displayMode == DISPLAY_MODE_DEPTH) { 
        outFinalColor = vec4(vec3(1.0 / (depth * 0.1 + 1.0)), 1.0); 
        return; 
    }
    if (displayMode == DISPLAY_MODE_MOTION) { 
        outFinalColor = vec4(vec3(abs(texture(gMotion, inUV).rg) * 10.0, 0.0), 1.0); 
        return; 
    }
    if (displayMode == DISPLAY_MODE_SHADOW) { outFinalColor = vec4(vec3(shadowFactor), 1.0); return; }
    if (displayMode == DISPLAY_MODE_AO) { outFinalColor = vec4(vec3(rtAO), 1.0); return; }
    if (displayMode == DISPLAY_MODE_GI) { outFinalColor = vec4(giIrradiance, 1.0); return; }
    if (displayMode == DISPLAY_MODE_REFLECTION) { outFinalColor = vec4(reflRadiance, 1.0); return; }

    // --- 5. 最终物理光照合成 (基于着色模型) ---
    vec3 worldPos = GetWorldPos(depth, inUV, camera.viewProjInverse);
    vec3 viewDir = normalize(camera.position.xyz - worldPos);
    vec3 lightDir = normalize(-sunLight.direction.xyz);
    vec3 lightIntensity = (renderFlags & RENDER_FLAG_LIGHT_BIT) != 0 ? (sunLight.color.rgb * sunLight.intensity.x) : vec3(0.0);

    // A. 直接光部分 (Cook-Torrance BRDF)
    vec3 directRadiance = EvalPbr(baseColor, 1.5, roughness, metallic, worldNormal, viewDir, lightDir) * shadowFactor * lightIntensity;

    // B. 间接漫反射 (GI + Albedo)
    vec3 F0 = mix(vec3(0.04), baseColor, metallic);
    vec3 F = FresnelSchlick(F0, worldNormal, viewDir);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 indirectDiffuse = giIrradiance * baseColor * kD; 

    // C. 间接镜面反射 (RT Reflections * Fresnel)
    vec3 indirectSpecular = reflRadiance * F;

    // D. 降级逻辑：若 GI 禁用则回退至简单的 AO 环境光
    if ((renderFlags & RENDER_FLAG_GI_BIT) == 0) {
        indirectDiffuse = ambStr * baseColor * rtAO * 0.1;
    }

    // --- 6. 色调映射与输出 ---
    vec3 finalColor = directRadiance + indirectDiffuse + indirectSpecular + emissive; 
    finalColor *= exposure;
    finalColor = pow(max(finalColor, vec3(0.0)), vec3(1.0 / 2.2)); // 伽马 2.2

    outFinalColor = vec4(finalColor, 1.0);
}
