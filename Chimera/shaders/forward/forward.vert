#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : enable
#include "ShaderCommon.h"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec2 outUV;
layout(location = 2) out vec3 outWorldPos;
layout(location = 3) out vec4 outTangent;
layout(location = 4) out flat uint outObjectId; // Pass ID to frag

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
    // Lookup data from SSBO using the pushed index
    GpuPrimitive prim = primBuf.primitives[pc.objectId];
    
    vec4 worldPos = prim.transform * vec4(inPosition, 1.0);
    outWorldPos = worldPos.xyz;
    
    mat3 normalMat = mat3(prim.normalMatrix);
    outNormal = normalize(normalMat * inNormal);
    outTangent = vec4(normalize(normalMat * inTangent.xyz), inTangent.w);
    
    outUV = inTexCoord;
    outObjectId = pc.objectId;
    
    gl_Position = global.ubo.camera.proj * global.ubo.camera.view * worldPos;
}
