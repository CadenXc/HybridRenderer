#version 460
#extension GL_EXT_ray_tracing : require

// 接收 Payload
layout(location = 0) rayPayloadInEXT vec3 hitValue;
// 内置变量：重心坐标 (用来插值属性)
hitAttributeEXT vec2 attribs;

void main() 
{
    // 简单的可视化：使用重心坐标作为颜色
    // 这会让物体看起来像彩色的线框图，非常适合调试
    const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
    hitValue = barycentricCoords;
}