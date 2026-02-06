#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec4 fragCurrPos;
layout(location = 3) in vec4 fragPrevPos;
layout(location = 4) in flat int inMaterialIndex;

layout(location = 0) out vec4 outAlbedo;   // RT0
layout(location = 1) out vec4 outNormal;   // RT1
layout(location = 2) out vec4 outMaterial; // RT2
layout(location = 3) out vec4 outMotion;   // RT3

struct Material
{
    vec4 albedo;
    vec4 emission;
    float metallic;
    float roughness;
    float alphaCutoff;
    int alphaMask;
    
    int base_color_texture;
    int normal_map;
    int metallic_roughness_map;
    int emissive_map;
};

layout(set = 1, binding = 0) readonly buffer MaterialBuffer {
    Material materials[];
} materialBuffer;

layout(set = 1, binding = 1) uniform sampler2D textures[];

void main() {
    Material mat = materialBuffer.materials[inMaterialIndex];
    
    // RT0: Albedo
    vec4 baseColor = mat.albedo;
    if (mat.base_color_texture >= 0) {
        baseColor *= texture(textures[nonuniformEXT(mat.base_color_texture)], fragTexCoord);
    }
    outAlbedo = vec4(baseColor.rgb + mat.emission.rgb, baseColor.a);

    // RT1: Normal (World Space)
    // TODO: Normal mapping
    outNormal = vec4(normalize(fragNormal) * 0.5 + 0.5, 1.0);

    // RT2: Material (Roughness, Metallic, AO, Object ID)
    outMaterial = vec4(mat.roughness, mat.metallic, 1.0, float(inMaterialIndex) / 255.0);

    // RT3: Motion Vectors
    vec2 a = (fragCurrPos.xy / fragCurrPos.w);
    vec2 b = (fragPrevPos.xy / fragPrevPos.w);
    outMotion = vec4(a - b, 0.0, 1.0);
}