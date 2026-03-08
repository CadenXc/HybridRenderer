#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common/common.glsl"

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inWorldPos;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in flat uint inObjectId; 
layout(location = 5) in vec4 inCurPos;
layout(location = 6) in vec4 inPrevPos;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outMotion;

void main() 
{
    // 1. 获取基础数据
    GpuPrimitive prim = primBuf.primitives[inObjectId];
    GpuMaterial mat = materialBuffer.m[prim.materialIndex];

    // 2. 解包材质参数
    vec4 albedo = GetAlbedo(mat, inUV);
    vec3 N = CalculateNormal(mat, inNormal, inTangent, inUV);
    vec3 V = normalize(global.ubo.camera.position.xyz - inWorldPos);
    
    float roughness = mat.roughness;
    float metallic = mat.metallic;
    if (mat.metalRoughTex >= 0)
    {
        vec4 mrSample = texture(textureArray[nonuniformEXT(mat.metalRoughTex)], inUV);
        roughness *= mrSample.g;
        metallic *= mrSample.b;
    }

    float ao = GetAmbientOcclusion(mat, inUV);
    vec3 emissive = GetEmissive(mat, inUV);

    // [MAGIC] 3. 物理面法线偏移 (消除阴影粉刺)
    vec3 ddx = dFdx(inWorldPos);
    vec3 ddy = dFdy(inWorldPos);
    vec3 faceNormal = normalize(cross(ddx, ddy));
    if (dot(faceNormal, V) < 0.0) 
    {
        faceNormal = -faceNormal;
    }

    // 4. 计算阴影
    vec3 L = normalize(-global.ubo.sunLight.direction.xyz);
    vec3 shadowOrigin = OffsetRay(inWorldPos, faceNormal);
    float shadow = CalculateRayQueryShadow(shadowOrigin, L, 10000.0);

    // 5. 最终着色
    vec3 lightColor = global.ubo.sunLight.color.rgb * global.ubo.sunLight.intensity.x;
    vec3 directLighting = EvaluateDirectPBR(N, V, L, albedo.rgb, roughness, metallic, lightColor);
    
    // 6. 环境光 (IBL + Ambient)
    vec3 ambient = global.ubo.postData.y * albedo.rgb * ao; // postData.y is ambientStrength
    int skyboxIdx = int(global.ubo.envData.x);

    if (skyboxIdx >= 0)
    {
        vec3 reflectDir = reflect(-V, N);
        vec3 envSpecular = texture(textureArray[nonuniformEXT(skyboxIdx)], SampleEquirectangular(reflectDir)).rgb;
        vec3 envDiffuse = texture(textureArray[nonuniformEXT(skyboxIdx)], SampleEquirectangular(N)).rgb;
        
        vec3 F0 = mix(vec3(0.04), albedo.rgb, metallic);
        vec3 F = F_SchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
        
        ambient = (kD * envDiffuse * albedo.rgb + kS * envSpecular) * ao * global.ubo.postData.y;
    }

    outColor = vec4(ambient + directLighting * shadow + emissive, albedo.a);

    // 7. 运动矢量 (为 TAA/SVGF 准备)
    vec2 cur = (inCurPos.xy / inCurPos.w) * 0.5 + 0.5;
    vec2 prev = (inPrevPos.xy / inPrevPos.w) * 0.5 + 0.5;
    outMotion = cur - prev;
}
