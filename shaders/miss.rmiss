#version 460
#extension GL_EXT_ray_tracing : require

// 接收 Payload
layout(location = 0) rayPayloadInEXT vec3 hitValue;

void main() 
{
    // 没有打中物体，返回一个渐变的天空色 (蓝紫色)
    // 这样我们能一眼看出光追生效了
    hitValue = vec3(0.0, 0.0, 0.2); 
    // 或者用红色测试： hitValue = vec3(1.0, 0.0, 0.0);
}