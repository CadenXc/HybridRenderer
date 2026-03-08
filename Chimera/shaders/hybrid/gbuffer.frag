#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common/common.glsl"

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in flat uint inObjectId;
layout(location = 3) in vec4 inCurPos;
layout(location = 4) in vec4 inPrevPos;
layout(location = 5) in vec4 inTangent;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outMaterial;
layout(location = 3) out vec2 outMotion;
layout(location = 4) out vec4 outEmissive;

void main() 
{
    // 1. 获取统一资源 (from common.glsl)
    GpuPrimitive prim = primBuf.primitives[inObjectId];
    GpuMaterial mat = materialBuffer.m[prim.materialIndex];
    
    // 2. 解包材质与法线
    vec4 albedo = GetAlbedo(mat, inTexCoord);
    if (albedo.a < 0.1) 
    {
        discard;
    }

    // 确保插值后的向量重新归一化
    vec3 N = CalculateNormal(mat, normalize(inNormal), inTangent, inTexCoord);

    // 3. Metallic-Roughness
    float metallic = mat.metallic;
    float roughness = mat.roughness;
    if (mat.metalRoughTex >= 0) 
    {
        vec4 mrSample = texture(textureArray[nonuniformEXT(mat.metalRoughTex)], inTexCoord);
        roughness *= mrSample.g;
        metallic *= mrSample.b;
    }

    // [NEW] 4. 获取 AO 和 Emissive
    float ao = GetAmbientOcclusion(mat, inTexCoord);
    vec3 emissive = GetEmissive(mat, inTexCoord);

    // [FINAL FIX] Pure geometric motion vector
    // We use interpolated non-jittered positions from vertex shader.
    // This is mathematically the most stable way to calculate motion.
    vec2 curUV  = (inCurPos.xy / inCurPos.w) * 0.5 + 0.5;
    vec2 prevUV = (inPrevPos.xy / inPrevPos.w) * 0.5 + 0.5;
    
    outMotion = curUV - prevUV;

    // 6. G-Buffer Output
    outAlbedo = vec4(albedo.rgb, 1.0);
    outNormal = vec4(N, 1.0);
    outMaterial = vec4(roughness, metallic, ao, 1.0);
    outEmissive = vec4(emissive, 1.0);
}
