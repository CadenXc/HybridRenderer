#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common/common.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec2 outTexCoord;
layout(location = 2) out flat uint outObjectId;
layout(location = 3) out vec4 outCurPos;
layout(location = 4) out vec4 outPrevPos;
layout(location = 5) out vec4 outTangent;

layout(push_constant) uniform PushConstants 
{
    ScenePushConstants pc;
};

void main()
{
    GpuPrimitive prim = primBuf.primitives[pc.objectId];
    
    // 1. 统一位置变换 (Using common.glsl)
    outCurPos = ProjectPosition(inPos, prim.transform);
    outPrevPos = ProjectPreviousPosition(inPos, prim.prevTransform);
    
    gl_Position = outCurPos;
    
    // 2. 法线与切线变换
    mat3 normalMat = mat3(prim.normalMatrix);
    outNormal = normalize(normalMat * inNormal);
    outTangent = vec4(normalize(normalMat * inTangent.xyz), inTangent.w);
    
    // 3. 通用输出
    outTexCoord = inTexCoord;
    outObjectId = pc.objectId;
}
