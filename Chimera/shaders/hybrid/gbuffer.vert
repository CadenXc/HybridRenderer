#version 460
#include "ShaderCommon.h"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inTexCoord;

layout(set = 0, binding = 0) uniform GlobalUBO {
    UniformBufferObject ubo;
} global;

layout(push_constant) uniform PushConstants {
    GBufferPushConstants pc;
} push;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec2 outTexCoord;
layout(location = 2) out flat int outMaterialIdx;
layout(location = 3) out vec4 outCurPos;
layout(location = 4) out vec4 outPrevPos;
layout(location = 5) out vec4 outTangent;

void main() {
    vec4 worldPos = push.pc.model * vec4(inPos, 1.0);
    vec4 prevWorldPos = push.pc.prevModel * vec4(inPos, 1.0);
    
    outCurPos = global.ubo.proj * global.ubo.view * worldPos;
    outPrevPos = global.ubo.prevProj * global.ubo.prevView * prevWorldPos;
    
    gl_Position = outCurPos;
    
    mat3 normalMat = mat3(push.pc.normalMatrix);
    outNormal = normalize(normalMat * inNormal);
    outTangent = vec4(normalize(normalMat * inTangent.xyz), inTangent.w);
    
    outTexCoord = inTexCoord;
    outMaterialIdx = push.pc.materialIndex;
}
