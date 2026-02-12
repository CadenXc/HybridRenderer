#version 460
#extension GL_EXT_nonuniform_qualifier : require
#include "ShaderCommon.h"

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in flat int inMaterialIdx;
layout(location = 3) in vec4 inCurPos;
layout(location = 4) in vec4 inPrevPos;

layout(set = 1, binding = 1) readonly buffer MaterialBuffer {
    PBRMaterial materials[];
} matBuf;

layout(set = 1, binding = 3) uniform sampler2D textureArray[];

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outMaterial;
layout(location = 3) out vec4 outMotion;

void main() {
    PBRMaterial mat = matBuf.materials[inMaterialIdx];
    
    vec4 albedo = mat.albedo;
    if (mat.albedoTex >= 0) {
        albedo *= texture(textureArray[nonuniformEXT(mat.albedoTex)], inTexCoord);
    }
    if (albedo.a < 0.1) discard;

    // --- 计算 Motion Vector ---
    vec2 a = (inCurPos.xy / inCurPos.w) * 0.5 + 0.5;
    vec2 b = (inPrevPos.xy / inPrevPos.w) * 0.5 + 0.5;
    // 恢复为标准的原始 UV 差值，供后续降噪/TAA 使用
    outMotion = vec4(a - b, 0.0, 1.0);

    outAlbedo = vec4(albedo.rgb, 1.0);
    outNormal = vec4(normalize(inNormal) * 0.5 + 0.5, 1.0);
    outMaterial = vec4(mat.metallic, mat.roughness, 0.0, 1.0);
}
