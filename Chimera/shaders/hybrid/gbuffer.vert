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
    GpuPrimitive prim = primitives[pc.objectId];
    
    // 1. 统一位置变换
    vec4 worldPos = LocalToWorld(inPos, prim.transform);
    vec4 prevWorldPos = LocalToWorld(inPos, prim.prevTransform);

    // 计算非抖动的投影坐标 (用于运动矢量)
    outCurPos = WorldToClip(worldPos);
    outPrevPos = PrevWorldToClip(prevWorldPos);
    
    // 2. 最终渲染坐标 (应用 TAA 抖动)
    // 抖动是在 NDC 空间应用的：pos.xy += jitter * pos.w
    gl_Position = outCurPos;
    gl_Position.xy += camera.jitterData.xy * gl_Position.w;
    
    // 3. 法线与切线变换
    mat3 normalMat = mat3(prim.normalMatrix);
    outNormal = normalize(normalMat * inNormal);
    outTangent = vec4(normalize(normalMat * inTangent.xyz), inTangent.w);
    
    // 4. 通用输出
    outTexCoord = inTexCoord;
    outObjectId = pc.objectId;
}
