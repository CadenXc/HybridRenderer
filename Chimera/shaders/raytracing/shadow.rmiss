#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common/common.glsl"

layout(location = 0) rayPayloadInEXT HitPayload payload;

void main() 
{
    // 如果触发了 Miss，说明光线没被挡住 -> 没阴影
    payload.motion_hit.b = 0.0;
}
