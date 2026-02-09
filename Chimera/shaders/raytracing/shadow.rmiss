#version 460
#extension GL_EXT_ray_tracing : require

// Standard Payload Location 1 for Shadows
layout(location = 1) rayPayloadInEXT bool isShadowed;

void main() 
{
    isShadowed = false;
}
