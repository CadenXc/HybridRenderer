#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT int isShadowed;

void main() 
{
    // 如果触发了 Miss，说明光线没被挡住 -> 没阴影
    isShadowed = 0;
}