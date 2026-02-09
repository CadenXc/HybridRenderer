#version 450
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : require

#include "../common/structures.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

struct DirectionalLight { mat4 projview; vec4 direction; vec4 color; vec4 intensity; };
layout(binding = 0, set = 0) uniform GlobalUBO {
    mat4 view; mat4 proj; mat4 viewInverse; mat4 projInverse; mat4 viewProjInverse;
    mat4 prevView; mat4 prevProj; DirectionalLight directionalLight;
    vec2 displaySize; vec2 displaySizeInverse; uint frameIndex; uint frameCount; uint displayMode; float padding; vec4 cameraPos;
} ubo;

// --- Standard Set 1 (Scene) ---
layout(binding = 0, set = 1) uniform accelerationStructureEXT SceneAS;
layout(binding = 1, set = 1, scalar) readonly buffer MaterialBuffer { PBRMaterial m[]; } materialBuffer;
layout(binding = 2, set = 1, scalar) readonly buffer InstanceBuffer { RTInstanceData i[]; } instanceBuffer;
layout(binding = 3, set = 1) uniform sampler2D textureArray[];

// --- Set 2: Pass Specific (GBuffer inputs for Deferred Pass) ---
layout(binding = 0, set = 2) uniform sampler2D u_Albedo;
layout(binding = 1, set = 2) uniform sampler2D u_Normal;
layout(binding = 2, set = 2) uniform sampler2D u_Material;
layout(binding = 3, set = 2) uniform sampler2D u_Depth;
layout(binding = 4, set = 2) uniform sampler2D u_ShadowAO;

void main() {
    vec3 albedo = texture(u_Albedo, inUV).rgb;
    vec3 normal = texture(u_Normal, inUV).xyz;
    vec4 mat = texture(u_Material, inUV);
    float shadow = texture(u_ShadowAO, inUV).r;

    // Simple deferred shading
    vec3 L = normalize(-ubo.directionalLight.direction.xyz);
    float NdotL = max(dot(normal, L), 0.05);
    vec3 lighting = albedo * ubo.directionalLight.color.rgb * ubo.directionalLight.intensity.x * NdotL;
    
    // Apply RT Shadow
    lighting *= shadow;

    outColor = vec4(lighting, 1.0);
}
