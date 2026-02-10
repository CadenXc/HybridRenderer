#version 450
#extension GL_GOOGLE_include_directive : require
#include "ShaderCommon.h"

// --- Inputs ---
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inTexCoord;

// --- Global Data ---
layout(set = 0, binding = 0) uniform GlobalUniforms 
{
    UniformBufferObject cam;
} ubo;

// --- Per-Object Data (Must match GBufferPushConstants in ShaderCommon.h) ---
layout(push_constant) uniform PushConstants 
{
    mat4 model;
    mat4 normalMatrix;
    int materialIndex;
} pc;

// --- Outputs ---
layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outTexCoord;
layout(location = 3) out mat3 outTBN;

void main() 
{
    // 1. Calculate World Space Position
    vec4 worldPosition = pc.model * vec4(inPosition, 1.0);
    outWorldPos = worldPosition.xyz;
    outTexCoord = inTexCoord;

    // 2. Calculate World Space Normal
    // PERFORMANCE OPTIMIZATION: Use the precomputed normalMatrix from CPU.
    // This avoids 100,000s of inverse() calls per frame on the GPU.
    mat3 worldNormalMatrix = mat3(pc.normalMatrix);
    vec3 worldNormal  = normalize(worldNormalMatrix * inNormal);
    outNormal = worldNormal;

    // 3. Build TBN (Tangent, Bitangent, Normal) Matrix for Normal Mapping
    vec3 worldTangent = normalize(worldNormalMatrix * inTangent.xyz);
    
    // Gram-Schmidt process: Re-orthogonalize Tangent with respect to Normal
    // This ensures T and N are perfectly 90 degrees even after interpolation.
    worldTangent = normalize(worldTangent - dot(worldTangent, worldNormal) * worldNormal);
    
    // Bitangent is the third axis, calculated via cross product.
    // The 'w' component of inTangent handles mirrored UVs (handedness).
    vec3 worldBitangent = cross(worldNormal, worldTangent) * inTangent.w;
    
    outTBN = mat3(worldTangent, worldBitangent, worldNormal);

    // 4. Final Projection
    gl_Position = ubo.cam.proj * ubo.cam.view * worldPosition;
}