#version 450
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_ray_tracing : enable
#extension GL_GOOGLE_include_directive : require

#include "../common/structures.glsl"

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in mat3 inTBN;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outMaterial;
layout(location = 3) out vec4 outMotion;

layout(push_constant) uniform ModelData {
    mat4 model;
    int materialIndex;
} pc;

// --- Standard Set 1 (Scene) ---
layout(binding = 0, set = 1) uniform accelerationStructureEXT SceneAS;
layout(binding = 1, set = 1, scalar) readonly buffer MaterialBuffer { PBRMaterial m[]; } materialBuffer;
layout(binding = 2, set = 1, scalar) readonly buffer InstanceBuffer { RTInstanceData i[]; } instanceBuffer;
layout(binding = 3, set = 1) uniform sampler2D textureArray[];

void main() {
    PBRMaterial mat = materialBuffer.m[pc.materialIndex];
    vec4 albedo = mat.albedo;
    if (mat.albedoTex >= 0) {
        albedo *= texture(textureArray[nonuniformEXT(mat.albedoTex)], inTexCoord);
    }

    outAlbedo = albedo;
    outNormal = vec4(normalize(inNormal), 1.0);
    outMaterial = vec4(mat.roughness, mat.metallic, float(pc.materialIndex), 1.0);
    outMotion = vec4(0.0); 
}