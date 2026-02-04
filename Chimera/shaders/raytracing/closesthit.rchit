#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_scalar_block_layout : enable

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
    vec4 lightPos;
    int  frameCount;
} cam;

layout(binding = 1, set = 0) uniform sampler2D globalTexture;

// ======================= [Set 1] RenderGraph 注入 =======================
layout(binding = 0, set = 1) uniform accelerationStructureEXT topLevelAS;

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

layout(binding = 3, set = 1, scalar) buffer Vertices { VertexData v[]; } vertices;
layout(binding = 4, set = 1, scalar) buffer Indices { uint i[]; } indices;

hitAttributeEXT vec2 attribs;

void main() 
{
    // 1. 获取几何数据
    uint ind0 = indices.i[3 * gl_PrimitiveID + 0];
    uint ind1 = indices.i[3 * gl_PrimitiveID + 1];
    uint ind2 = indices.i[3 * gl_PrimitiveID + 2];

    VertexData v0 = vertices.v[ind0];
    VertexData v1 = vertices.v[ind1];
    VertexData v2 = vertices.v[ind2];

    const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
    vec3 localPos = v0.pos * barycentricCoords.x + v1.pos * barycentricCoords.y + v2.pos * barycentricCoords.z;
    vec3 worldPos = vec3(cam.model * vec4(localPos, 1.0));
    
    vec3 normal = v0.normal * barycentricCoords.x + v1.normal * barycentricCoords.y + v2.normal * barycentricCoords.z;
    vec3 worldNormal = normalize(mat3(transpose(inverse(cam.model))) * normal);
    
    vec2 texCoord = v0.texCoord * barycentricCoords.x + v1.texCoord * barycentricCoords.y + v2.texCoord * barycentricCoords.z;

    // 使用全局纹理 (Set 0)
    vec3 albedo = textureLod(globalTexture, texCoord, 0.0).rgb;

    vec3 lightPos = cam.lightPos.xyz;
    vec3 L = normalize(lightPos - worldPos);
    float dotNL = max(dot(worldNormal, L), 0.2);
    vec3 diffuseColor = albedo * dotNL;

    payload.color    = diffuseColor;
    payload.normal   = worldNormal;
    payload.worldPos = worldPos;
    payload.depth    = 1;
}