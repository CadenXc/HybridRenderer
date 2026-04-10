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
    GpuInstance inst = instances[pc.objectId];
    mat3 normalMat = mat3(inst.normalTransform);

    // [Location 0]: 世界空间法线
    outNormal = normalize(normalMat * inNormal);

    // [Location 1]: UV 坐标
    outTexCoord = inTexCoord;

    // [Location 2]: 物体 ID
    outObjectId = pc.objectId;

    // [Location 3 & 4]: 运动矢量计算所需位置
    vec4 worldPos = LocalToWorld(inPos, inst.transform);
    vec4 prevWorldPos = LocalToWorld(inPos, inst.prevTransform);
    outCurPos = WorldToClip(worldPos);
    outPrevPos = PrevWorldToClip(prevWorldPos);
    
    // [Location 5]: 切线空间向量
    outTangent = vec4(normalize(normalMat * inTangent.xyz), inTangent.w);

    // 系统输出：应用 TAA 抖动的最终位置
    gl_Position = outCurPos;
    gl_Position.xy += camera.jitterData.xy * gl_Position.w;
}
