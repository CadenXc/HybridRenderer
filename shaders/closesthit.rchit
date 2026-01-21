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

// 输入 Payload (Location 0)
layout(location = 0) rayPayloadInEXT HitPayload payload;

layout(binding = 1, set = 1) uniform sampler2D texSampler;

// [新增] 必须和 C++ 及 RayGen 里的定义完全一致
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

layout(binding = 2, set = 0, scalar) buffer Vertices { VertexData v[]; } vertices;
layout(binding = 3, set = 0, scalar) buffer Indices { uint i[]; } indices;

void main() 
{
    // -----------------------------------------------------------------
    // 1. 获取几何数据
    // -----------------------------------------------------------------
    uint ind0 = indices.i[3 * gl_PrimitiveID + 0];
    uint ind1 = indices.i[3 * gl_PrimitiveID + 1];
    uint ind2 = indices.i[3 * gl_PrimitiveID + 2];

    VertexData v0 = vertices.v[ind0];
    VertexData v1 = vertices.v[ind1];
    VertexData v2 = vertices.v[ind2];

    // 重心坐标插值
    const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
    
    // 计算世界坐标位置
    vec3 worldPos = v0.pos * barycentricCoords.x + v1.pos * barycentricCoords.y + v2.pos * barycentricCoords.z;

    // 计算面法线
    vec3 edge1 = v1.pos - v0.pos;
    vec3 edge2 = v2.pos - v0.pos;
    vec3 worldNormal = normalize(cross(edge1, edge2));

    // 计算 UV 坐标
    vec2 texCoord = v0.texCoord * barycentricCoords.x + v1.texCoord * barycentricCoords.y + v2.texCoord * barycentricCoords.z;

    // -----------------------------------------------------------------
    // 2. 纹理采样 & 光照 (修复点在此)
    // -----------------------------------------------------------------
    
    // [修复] 使用 textureLod 强制读取 Level 0
    // 普通的 texture() 在光追 Shader 中因为缺少导数信息，经常会采样失败变黑
    vec3 albedo = textureLod(texSampler, texCoord, 0.0).rgb;

    // [可选调试] 如果纹理依然黑，取消下面这行的注释，直接看 UV 是否正确 (红绿渐变)
    // payload.color = vec3(texCoord, 0.0); payload.depth = 1; return;

    vec3 lightPos = cam.lightPos.xyz;

    vec3 L = normalize(lightPos - worldPos);
    
    // 简单的漫反射
    float dotNL = max(dot(worldNormal, L), 0.2);
    
    // 最终漫反射颜色 = 纹理颜色 * 光照强度
    vec3 diffuseColor = albedo * dotNL;

    // -----------------------------------------------------------------
    // 3. 将结果写入 Payload 返回给 RayGen
    // -----------------------------------------------------------------
    payload.color    = diffuseColor;
    payload.normal   = worldNormal;
    payload.worldPos = worldPos;
    payload.depth    = 1;
}