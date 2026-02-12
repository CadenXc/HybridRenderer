#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT int isShadowed;

void main() 
{
    isShadowed = 0;
}