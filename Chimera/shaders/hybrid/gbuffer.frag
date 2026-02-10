#version 450
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : require
#include "ShaderCommon.h"

// --- Inputs from Vertex Shader ---
layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in mat3 inTBN;

// --- Resource Bindings ---
// Set 1: Scene-wide resources (Must match ResourceManager.cpp layout)
layout(set = 1, binding = 1) readonly buffer MaterialBuffer 
{
    PBRMaterial materials[];
} materialBuffer;

layout(set = 1, binding = 3) uniform sampler2D textureArray[];

// --- G-Buffer Outputs ---
layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outMaterial;

// --- Per-Object Data (Must match GBufferPushConstants in ShaderCommon.h) ---
layout(push_constant) uniform PushConstants 
{
    mat4 model;
    mat4 normalMatrix;
    int materialIndex;
} pc;

void main() 
{
    // 1. Fetch Material Properties
    PBRMaterial material = materialBuffer.materials[pc.materialIndex];

    // 2. Calculate Albedo (Base Color)
    vec4 baseColor = material.albedo;
    if (material.albedoTex >= 0) 
    {
        // Use nonuniformEXT for dynamic indexing into the global texture array
        baseColor *= texture(textureArray[nonuniformEXT(material.albedoTex)], inTexCoord);
    }
    outAlbedo = baseColor;

    // 3. Calculate World Space Normal
    // Interpolated normals from the vertex shader must be re-normalized in the fragment shader.
    vec3 worldNormal = normalize(inNormal);

    if (material.normalTex >= 0) 
    {
        // Normal Map Processing:
        // a. Sample the normal from texture (usually stored in [0, 1] range)
        vec3 sampledNormal = texture(textureArray[nonuniformEXT(material.normalTex)], inTexCoord).xyz;
        
        // b. Remap from RGB [0, 1] to Directional [-1, 1]
        sampledNormal = sampledNormal * 2.0 - 1.0;
        
        // c. Transform the tangent-space normal into world-space using the TBN matrix
        worldNormal = normalize(inTBN * sampledNormal);
    }
    
    // Store as SFLOAT16 for high precision in lighting passes
    outNormal = vec4(worldNormal, 1.0);

    // 4. Calculate PBR Material Parameters (Roughness, Metallic)
    float roughness = material.roughness;
    float metallic = material.metallic;
    float ao = 1.0; // Default Ambient Occlusion

    if (material.metalRoughTex >= 0) 
    {
        // Standard glTF packing (ORM texture):
        // R: Occlusion (G-Buffer B channel)
        // G: Roughness (G-Buffer R channel)
        // B: Metallic  (G-Buffer G channel)
        vec4 ormSample = texture(textureArray[nonuniformEXT(material.metalRoughTex)], inTexCoord);
        
        ao *= ormSample.r;
        roughness *= ormSample.g;
        metallic  *= ormSample.b;
    }

    // Write to RT2 (Material G-Buffer):
    // Red: Roughness
    // Green: Metallic
    // Blue: Occlusion (AO)
    // Alpha: Shading Model (1.0 for Standard PBR)
    outMaterial = vec4(roughness, metallic, ao, 1.0);
}
