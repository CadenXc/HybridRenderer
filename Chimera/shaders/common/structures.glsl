#ifndef STRUCTURES_GLSL
#define STRUCTURES_GLSL

#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable

struct PBRMaterial
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

struct RTInstanceData
{
    uint64_t vertexAddress;
    uint64_t indexAddress;
    int materialIndex;
    int padding;
};

struct HitPayload
{
    vec3 color;
    vec3 attenuation;
    vec3 rayOrigin;
    vec3 rayDir;
    bool hit;
    int  depth;
};

#endif