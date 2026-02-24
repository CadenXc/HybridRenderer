#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : enable
#include "ShaderCommon.h"

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

layout(set = 0, binding = 0) uniform GlobalUBO { UniformBufferObject ubo; } global;

// [NEW] SSBO Binding for Primitives (Set 1, Binding 2)
layout(set = 1, binding = 2, scalar) readonly buffer PrimitiveBuffer 
{
    GpuPrimitive primitives[];
} primBuf;

layout(push_constant) uniform PushConstants 
{
    ScenePushConstants pc;
};

void main()
{
    GpuPrimitive prim = primBuf.primitives[pc.objectId];
    
    vec4 worldPos = prim.transform * vec4(inPos, 1.0);
    vec4 prevWorldPos = prim.prevTransform * vec4(inPos, 1.0);
    
    outCurPos = global.ubo.camera.proj * global.ubo.camera.view * worldPos;
    outPrevPos = global.ubo.camera.prevProj * global.ubo.camera.prevView * prevWorldPos;
    
    gl_Position = outCurPos;
    
    mat3 normalMat = mat3(prim.normalMatrix);
    outNormal = normalize(normalMat * inNormal);
    outTangent = vec4(normalize(normalMat * inTangent.xyz), inTangent.w);
    
    outTexCoord = inTexCoord;
    outObjectId = pc.objectId;
}
