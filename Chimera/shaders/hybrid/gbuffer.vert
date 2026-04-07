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
    
    vec4 worldPos = LocalToWorld(inPos, inst.transform);
    vec4 prevWorldPos = LocalToWorld(inPos, inst.prevTransform);

    outCurPos = WorldToClip(worldPos);
    outPrevPos = PrevWorldToClip(prevWorldPos);
    
    gl_Position = outCurPos;
    gl_Position.xy += camera.jitterData.xy * gl_Position.w;
    
    mat3 normalMat = mat3(inst.normalTransform);
    outNormal = normalize(normalMat * inNormal);
    outTangent = vec4(normalize(normalMat * inTangent.xyz), inTangent.w);
    
    outTexCoord = inTexCoord;
    outObjectId = pc.objectId;
}
