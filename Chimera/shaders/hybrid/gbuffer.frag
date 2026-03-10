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
    GpuPrimitive prim = primitives[inObjectId];
    GpuMaterial mat = materials[prim.materialIndex];
    
    vec4 albedoSample = GetAlbedo(mat, inTexCoord);
    vec3 baseColor = albedoSample.rgb;
    if (albedoSample.a < 0.1) 
    {
        discard;
    }

    vec3 worldNormal = CalculateNormal(mat, normalize(inNormal), inTangent, inTexCoord);

    float metallic = mat.metallic;
    float roughness = mat.roughness;
    if (mat.metalRoughTex >= 0) 
    {
        vec4 mrSample = texture(textureArray[nonuniformEXT(mat.metalRoughTex)], inTexCoord);
        roughness *= mrSample.g;
        metallic *= mrSample.b;
    }

    float ao = GetAmbientOcclusion(mat, inTexCoord);
    vec3 emissive = GetEmissive(mat, inTexCoord);

    vec2 curUV  = (inCurPos.xy / inCurPos.w) * 0.5 + 0.5;
    vec2 prevUV = (inPrevPos.xy / inPrevPos.w) * 0.5 + 0.5;
    
    outMotion = curUV - prevUV;

    outAlbedo = vec4(baseColor, 1.0);
    outNormal = vec4(worldNormal, 1.0);
    outMaterial = vec4(roughness, metallic, ao, 1.0);
    outEmissive = vec4(emissive, 1.0);
}
