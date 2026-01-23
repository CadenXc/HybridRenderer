#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_scalar_block_layout : enable

// ======================= Payload 定义 =======================
struct HitPayload
{
    vec3 color;
    vec3 normal;
    vec3 worldPos;
    int  depth;
};

layout(location = 0) rayPayloadInEXT HitPayload payload;
layout(binding = 1, set = 1) uniform sampler2D texSampler;

layout(binding = 0, set = 1) uniform CameraProperties 
{
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 lightPos;
} cam;

hitAttributeEXT vec2 attribs;

// ======================= 资源绑定 =======================
layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;

struct VertexData
{
    vec3 pos;
    vec3 color;
    vec2 texCoord;
};

// [修改] Binding 2 -> 3 (因为 Binding 2 现在是 accumImage)
layout(binding = 3, set = 0, scalar) buffer Vertices { VertexData v[]; } vertices;

// [修改] Binding 3 -> 4
layout(binding = 4, set = 0, scalar) buffer Indices { uint i[]; } indices;

void main() 
{
    // ... (后续代码完全不用变) ...
    
    // -----------------------------------------------------------------
    // 1. 获取几何数据
    // -----------------------------------------------------------------
    uint ind0 = indices.i[3 * gl_PrimitiveID + 0];
    uint ind1 = indices.i[3 * gl_PrimitiveID + 1];
    uint ind2 = indices.i[3 * gl_PrimitiveID + 2];

    VertexData v0 = vertices.v[ind0];
    VertexData v1 = vertices.v[ind1];
    VertexData v2 = vertices.v[ind2];

    const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
    vec3 worldPos = v0.pos * barycentricCoords.x + v1.pos * barycentricCoords.y + v2.pos * barycentricCoords.z;
    vec3 edge1 = v1.pos - v0.pos;
    vec3 edge2 = v2.pos - v0.pos;
    vec3 worldNormal = normalize(cross(edge1, edge2));
    
    vec2 texCoord = v0.texCoord * barycentricCoords.x + v1.texCoord * barycentricCoords.y + v2.texCoord * barycentricCoords.z;

    // 使用 textureLod 强制读取 Level 0
    vec3 albedo = textureLod(texSampler, texCoord, 0.0).rgb;

    vec3 lightPos = cam.lightPos.xyz;
    vec3 L = normalize(lightPos - worldPos);
    float dotNL = max(dot(worldNormal, L), 0.2);
    vec3 diffuseColor = albedo * dotNL;

    payload.color    = diffuseColor;
    payload.normal   = worldNormal;
    payload.worldPos = worldPos;
    payload.depth    = 1;
}