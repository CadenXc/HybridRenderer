#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in mat3 inTBN;

struct PBRMaterial {
    vec4 albedo;
    vec3 emission;
    float roughness;
    float metallic;
    int albedoTex;
    int normalTex;
    int metalRoughTex;
};

layout(set = 1, binding = 0) readonly buffer MaterialBuffer {
    PBRMaterial materials[];
} materialBuffer;

layout(set = 1, binding = 1) uniform sampler2D TextureArray[];

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outMaterial;
layout(location = 3) out vec4 outMotion;

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
    outMotion = vec4(0.0);
}