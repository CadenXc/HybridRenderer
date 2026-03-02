#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common/common.glsl"

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inWorldPos;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in flat uint inObjectId; 

layout(location = 0) out vec4 outColor;

void main() 
{
    // 1. 获取基础数据
    GpuPrimitive prim = primBuf.primitives[inObjectId];
    GpuMaterial mat = materialBuffer.m[prim.materialIndex];

    // 2. 解包材质参数
    vec4 albedo = GetAlbedo(mat, inUV);
    vec3 N = CalculateNormal(prim, mat, inNormal, inTangent, inUV);
    vec3 V = normalize(global.ubo.camera.position.xyz - inWorldPos);
    
    float roughness = mat.roughness;
    float metallic = mat.metallic;
    if (mat.metalRoughTex >= 0)
    {
        vec4 mrSample = texture(textureArray[nonuniformEXT(mat.metalRoughTex)], inUV);
        roughness *= mrSample.g;
        metallic *= mrSample.b;
    }

    // [NEW] 3. 获取 AO 和 Emissive
    float ao = GetAmbientOcclusion(mat, inUV);
    vec3 emissive = GetEmissive(mat, inUV);

    // 4. 计算光照与阴影
    vec3 L = normalize(-global.ubo.sunLight.direction.xyz);
    float shadow = CalculateRayQueryShadow(OffsetRay(inWorldPos, N), L, 10000.0);

    // 5. 使用统一的 PBR 评估函数
    vec3 lightColor = global.ubo.sunLight.color.rgb * global.ubo.sunLight.intensity.x;
    vec3 directLighting = EvaluateDirectPBR(N, V, L, albedo.rgb, roughness, metallic, lightColor);
    
    // 6. 环境光 (Apply AO)
    vec3 ambient = global.ubo.ambientStrength * albedo.rgb * ao;

    // 7. 最终合并 (Direct light is shadowed, ambient and emissive are not)
    outColor = vec4(ambient + directLighting * shadow + emissive, albedo.a);
}
