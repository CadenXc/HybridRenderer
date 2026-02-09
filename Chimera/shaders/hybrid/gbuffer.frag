#version 450
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : require
#include "ShaderCommon.h"

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in mat3 inTBN;

layout(set = 1, binding = 0) readonly buffer MaterialBuffer {
    PBRMaterial materials[];
} materialBuffer;

layout(set = 1, binding = 1) uniform sampler2D TextureArray[];

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outMaterial;
layout(location = 3) out vec4 outMaterial2; // Renamed location 3 to match GBufferPass if needed

layout(push_constant) uniform ModelData {
    mat4 model;
    uint materialIndex;
} pc;

void main() {
    PBRMaterial mat = materialBuffer.materials[pc.materialIndex];
    vec4 albedo = mat.albedo;
    if (mat.albedoTex >= 0) {
        albedo *= texture(TextureArray[nonuniformEXT(mat.albedoTex)], inTexCoord);
    }
    outAlbedo = albedo;

    vec3 N = normalize(inNormal);
    if (mat.normalTex >= 0) {
        vec3 tangentNormal = texture(TextureArray[nonuniformEXT(mat.normalTex)], inTexCoord).xyz * 2.0 - 1.0;
        N = normalize(inTBN * tangentNormal);
    }
    outNormal = vec4(N, 1.0);

    float roughness = mat.roughness;
    float metallic = mat.metallic;
    if (mat.metalRoughTex >= 0) {
        vec4 mrSample = texture(TextureArray[nonuniformEXT(mat.metalRoughTex)], inTexCoord);
        roughness *= mrSample.g;
        metallic *= mrSample.b;
    }
    outMaterial = vec4(roughness, metallic, 0.0, 1.0);
    outMaterial2 = vec4(0.0); // Motion vectors or other data
}