#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common/common.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outFinalColor;

// --- G-Buffer & Lighting Inputs (Set 2) ---
layout(set = 2, binding = 0) uniform sampler2D gAlbedo;
layout(set = 2, binding = 1) uniform sampler2D gShadowAO_Filtered;
layout(set = 2, binding = 2) uniform sampler2D gShadowAO_Raw;
layout(set = 2, binding = 3) uniform sampler2D gReflection_Filtered;
layout(set = 2, binding = 4) uniform sampler2D gReflection_Raw;
layout(set = 2, binding = 5) uniform sampler2D gGI_Filtered;
layout(set = 2, binding = 6) uniform sampler2D gGI_Raw;
layout(set = 2, binding = 7) uniform sampler2D gMaterial;
layout(set = 2, binding = 8) uniform sampler2D gNormal;
layout(set = 2, binding = 9) uniform sampler2D gDepth;
layout(set = 2, binding = 10) uniform sampler2D gMotion;

layout(push_constant) uniform PushConstants 
{
    int skyboxIndex;
} pc;

void main() 
{
    float depth = texture(gDepth, inUV).r;
    
    // 1. 背景处理
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
            outFinalColor = global.ubo.clearColor;
        }
        return;
    }

    // 2. 采样 G-Buffer 数据
    bool denoisingEnabled = (global.ubo.renderFlags & RENDER_FLAG_SVGF_BIT) != 0;
    
    vec3 albedo = texture(gAlbedo, inUV).rgb;
    vec3 N = normalize(texture(gNormal, inUV).xyz * 2.0 - 1.0);
    vec4 material = texture(gMaterial, inUV);
    
    float roughness = max(material.r, 0.05); 
    float metallic = material.g;
    
    // 从混合管线的其他 Pass 拿到信号 (阴影, 反射, GI)
    float shadowFactor = denoisingEnabled ? texture(gShadowAO_Filtered, inUV).r : texture(gShadowAO_Raw, inUV).r;
    vec3 reflection = denoisingEnabled ? texture(gReflection_Filtered, inUV).rgb : texture(gReflection_Raw, inUV).rgb;
    vec3 gi = denoisingEnabled ? texture(gGI_Filtered, inUV).rgb : texture(gGI_Raw, inUV).rgb;

    // --- [NEW] 这里我们可以通过 G-Buffer 挂载的索引反查材质数据，拿到 AO 和 Emissive ---
    // 为了简单，我们目前主要依赖光追 Pass 已经在 payload 里考虑了这些。
    // 但在混合渲染后期，我们依然可以从 MaterialBuffer 拿数据：
    // vec3 worldPos = GetWorldPos(depth, inUV, global.ubo.camera.viewProjInverse);
    // ...
    
    // 3. 调试视图
    if (global.ubo.displayMode == DISPLAY_MODE_ALBEDO) { outFinalColor = vec4(albedo, 1.0); return; }
    if (global.ubo.displayMode == DISPLAY_MODE_NORMAL) { outFinalColor = vec4(N * 0.5 + 0.5, 1.0); return; }
    
    // 4. 物理合成逻辑
    vec3 worldPos = GetWorldPos(depth, inUV, global.ubo.camera.viewProjInverse);
    vec3 V = normalize(global.ubo.camera.position.xyz - worldPos);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // 4.1 直接光
    vec3 L = normalize(-global.ubo.sunLight.direction.xyz);
    vec3 lightIntensity = global.ubo.sunLight.color.rgb * global.ubo.sunLight.intensity.x;
    vec3 directLighting = EvaluateDirectPBR(N, V, L, albedo, roughness, metallic, lightIntensity) * shadowFactor;

    // 4.2 间接光与环境光 (整合 GI 和 反射)
    float cosTheta = max(dot(N, V), 0.0);
    vec3 F_ind = F_SchlickRoughness(cosTheta, F0, roughness);
    
    vec3 kS_ind = F_ind;
    vec3 kD_ind = (vec3(1.0) - kS_ind) * (1.0 - metallic);
    
    vec3 ambientDiffuse = gi * kD_ind * global.ubo.ambientStrength;
    vec3 ambientSpecular = reflection * kS_ind * global.ubo.ambientStrength;
    
    // 这里我们目前假设 emissive 已经在直接光路径中计算或通过额外 pass 叠加
    vec3 finalColor = directLighting + ambientDiffuse + ambientSpecular;

    // 5. 后期处理
    finalColor *= global.ubo.exposure;
    finalColor = ACESToneMapping(finalColor);
    finalColor = pow(finalColor, vec3(1.0 / 2.2)); 

    outFinalColor = vec4(finalColor, 1.0);
}
