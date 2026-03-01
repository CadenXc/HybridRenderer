#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common/common.glsl"

layout(location = 0) rayPayloadInEXT HitPayload payload;

void main() 
{
    // 未击中物体时，告知 Payload
    payload.hit = false;
    payload.color = global.ubo.clearColor.rgb;
}
