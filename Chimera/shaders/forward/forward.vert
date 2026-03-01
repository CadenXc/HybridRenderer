#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common/common.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec2 outUV;
layout(location = 2) out vec3 outWorldPos;
layout(location = 3) out vec4 outTangent;
layout(location = 4) out flat uint outObjectId; 
layout(location = 5) out vec4 outCurPos;
layout(location = 6) out vec4 outPrevPos;

layout(push_constant) uniform PushConstants 
{
    ScenePushConstants pc;
};

void main() 
{
    GpuPrimitive prim = primBuf.primitives[pc.objectId];
    
    // 1. 统一位置变换与世界空间计算
    vec4 worldPos = LocalToWorld(inPosition, prim.transform);
    outWorldPos = worldPos.xyz;
    
    outCurPos = ProjectPosition(inPosition, prim.transform);
    outPrevPos = ProjectPreviousPosition(inPosition, prim.prevTransform);
    
    gl_Position = outCurPos;
    
    // 2. 法线与切线变换
    mat3 normalMat = mat3(prim.normalMatrix);
    outNormal = normalize(normalMat * inNormal);
    outTangent = vec4(normalize(normalMat * inTangent.xyz), inTangent.w);
    
    // 3. 通用输出
    outUV = inTexCoord;
    outObjectId = pc.objectId;
}
