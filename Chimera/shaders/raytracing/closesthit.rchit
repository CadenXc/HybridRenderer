#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

// ======================= Payload =======================
struct HitPayload
{
    vec3 color;
    vec3 normal;
    vec3 worldPos;
    int  depth;
};

layout(location = 0) rayPayloadInEXT HitPayload payload;

// ======================= [Set 0] 全局资源 =======================
layout(binding = 0, set = 0) uniform CameraProperties 
{
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 prevView;
    mat4 prevProj;
    vec4 cameraPos;
    vec4 lightPos;
    float time;
    int frameCount;
} cam;

layout(binding = 1, set = 0) uniform sampler2D globalTexture;

// ======================= [Set 1] RenderGraph 注入 =======================
layout(binding = 0, set = 1) uniform accelerationStructureEXT topLevelAS;
layout(binding = 1, set = 1, rgba8) uniform image2D rtOutput; 

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

layout(binding = 2, set = 1, scalar) buffer MaterialBuffer { Material m[]; } materials;

struct VertexData
{
    vec3 pos;
    float pad1;
    vec3 normal;
    float pad2;
    vec4 tangent;
    vec2 texCoord;
    vec2 pad3;
};

layout(buffer_reference, scalar) buffer Vertices { VertexData v[]; };
layout(buffer_reference, scalar) buffer Indices { uint i[]; };

struct InstanceData
{
    uint64_t vertexAddress;
    uint64_t indexAddress;
    int materialIndex;
    int padding;
};

layout(binding = 3, set = 1, scalar) buffer InstanceDataBuffer { InstanceData i[]; } instances;

layout(binding = 4, set = 1) uniform sampler2D textureArray[];

hitAttributeEXT vec2 attribs;

void main() 
{
    // 1. 获取当前实例的数据
    InstanceData inst = instances.i[gl_InstanceCustomIndexEXT];
    Vertices vertices = Vertices(inst.vertexAddress);
    Indices indices = Indices(inst.indexAddress);

    // 2. 获取几何数据
    uint ind0 = indices.i[3 * gl_PrimitiveID + 0];
    uint ind1 = indices.i[3 * gl_PrimitiveID + 1];
    uint ind2 = indices.i[3 * gl_PrimitiveID + 2];

    VertexData v0 = vertices.v[ind0];
    VertexData v1 = vertices.v[ind1];
    VertexData v2 = vertices.v[ind2];

    const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
    vec3 localPos = v0.pos * barycentricCoords.x + v1.pos * barycentricCoords.y + v2.pos * barycentricCoords.z;
    vec3 worldPos = vec3(gl_ObjectToWorldEXT * vec4(localPos, 1.0));
    
    vec3 normal = v0.normal * barycentricCoords.x + v1.normal * barycentricCoords.y + v2.normal * barycentricCoords.z;
    vec3 worldNormal = normalize(vec3(normal * gl_WorldToObjectEXT));
    
    vec2 texCoord = v0.texCoord * barycentricCoords.x + v1.texCoord * barycentricCoords.y + v2.texCoord * barycentricCoords.z;

    // 3. 获取材质信息
    Material mat = materials.m[inst.materialIndex];

    vec3 albedo = mat.albedo.rgb;
    if (mat.base_color_texture >= 0) {
        albedo *= textureLod(textureArray[nonuniformEXT(mat.base_color_texture)], texCoord, 0.0).rgb;
    }

    vec3 lightPos = cam.lightPos.xyz;
    vec3 L = normalize(lightPos - worldPos);
    float dotNL = max(dot(worldNormal, L), 0.2);
    vec3 diffuseColor = albedo * dotNL + mat.emission.rgb;

    payload.color    = diffuseColor;
    payload.normal   = worldNormal;
    payload.worldPos = worldPos;
    payload.depth    = 1;
}