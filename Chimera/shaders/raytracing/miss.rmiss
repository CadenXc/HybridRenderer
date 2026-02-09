#version 460
#extension GL_EXT_ray_tracing : require

#include "ShaderCommon.h"

layout(location = 0) rayPayloadInEXT HitPayload payload;

void main() 
{
    payload.hit = false;
}
