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
layout(location = 3) out vec4 outMotion;

void main() 
{
    // 1. 获取统一资源 (from common.glsl)
    GpuPrimitive prim = primBuf.primitives[inObjectId];
    GpuMaterial mat = materialBuffer.m[prim.materialIndex];
    
    // 2. 解包材质与法线 (Using helpers from common.glsl)
    vec4 albedo = GetAlbedo(mat, inTexCoord);
    if (albedo.a < 0.1) 
    {
        discard;
    }

    vec3 N = CalculateNormal(prim, mat, inNormal, inTangent, inTexCoord);

    // 3. Metallic-Roughness (Direct access as it's pass-specific logic)
    float metallic = mat.metallic;
    float roughness = mat.roughness;
    if (mat.metalRoughTex >= 0) 
    {
        vec4 mrSample = texture(textureArray[nonuniformEXT(mat.metalRoughTex)], inTexCoord);
        roughness *= mrSample.g;
        metallic *= mrSample.b;
    }

    // 4. Motion Vector
    vec2 a = (inCurPos.xy / inCurPos.w) * 0.5 + 0.5;
    vec2 b = (inPrevPos.xy / inPrevPos.w) * 0.5 + 0.5;
    outMotion = vec4(a - b, 0.0, 1.0);

    // 5. G-Buffer Output
    outAlbedo = vec4(albedo.rgb, 1.0);
    outNormal = vec4(N * 0.5 + 0.5, 1.0);
    outMaterial = vec4(roughness, metallic, 0.0, 1.0);
}
