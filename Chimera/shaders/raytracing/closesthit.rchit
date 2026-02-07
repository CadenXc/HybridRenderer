#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

#include "../common/structures.glsl"
#include "../common/pbr.glsl"

layout(location = 0) rayPayloadInEXT HitPayload payload;
layout(location = 1) rayPayloadEXT bool isShadowed;

struct DirectionalLight { mat4 projview; vec4 direction; vec4 color; vec4 intensity; };
layout(binding = 0, set = 0) uniform CameraProperties {
    mat4 view; mat4 proj; mat4 viewInverse; mat4 projInverse; mat4 viewProjInverse;
    mat4 prevView; mat4 prevProj; DirectionalLight directionalLight;
    vec2 displaySize; vec2 displaySizeInverse; uint frameIndex; uint frameCount; uint displayMode; vec4 cameraPos;
} cam;

layout(binding = 0, set = 1) uniform accelerationStructureEXT topLevelAS;
layout(binding = 2, set = 1, scalar) buffer MaterialBuffer { PBRMaterial m[]; } materials;
layout(binding = 3, set = 1, scalar) buffer InstanceDataBuffer { RTInstanceData i[]; } instances;
layout(binding = 4, set = 1) uniform sampler2D textureArray[];

struct VertexData { vec4 pos; vec4 normal; vec4 tangent; vec4 texCoord; };
layout(buffer_reference, scalar) buffer Vertices { VertexData v[]; };
layout(buffer_reference, scalar) buffer Indices { uint i[]; };

hitAttributeEXT vec2 attribs;

void main() 
{
    RTInstanceData inst = instances.i[gl_InstanceCustomIndexEXT];
    Vertices vertices = Vertices(inst.vertexAddress);
    Indices indices = Indices(inst.indexAddress);

    uint ind0 = indices.i[3 * gl_PrimitiveID + 0];
    uint ind1 = indices.i[3 * gl_PrimitiveID + 1];
    uint ind2 = indices.i[3 * gl_PrimitiveID + 2];

    VertexData v0 = vertices.v[ind0];
    VertexData v1 = vertices.v[ind1];
    VertexData v2 = vertices.v[ind2];

    const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
    vec3 localPos = v0.pos.xyz * barycentricCoords.x + v1.pos.xyz * barycentricCoords.y + v2.pos.xyz * barycentricCoords.z;
    vec3 worldPos = vec3(gl_ObjectToWorldEXT * vec4(localPos, 1.0));
    vec3 normal = v0.normal.xyz * barycentricCoords.x + v1.normal.xyz * barycentricCoords.y + v2.normal.xyz * barycentricCoords.z;
    vec3 worldNormal = normalize(vec3(normal * gl_WorldToObjectEXT));
    vec4 tangent = v0.tangent * barycentricCoords.x + v1.tangent * barycentricCoords.y + v2.tangent * barycentricCoords.z;
    vec3 worldTangent = normalize(mat3(gl_ObjectToWorldEXT) * tangent.xyz);
    vec2 texCoord = v0.texCoord.xy * barycentricCoords.x + v1.texCoord.xy * barycentricCoords.y + v2.texCoord.xy * barycentricCoords.z;

    PBRMaterial mat = materials.m[inst.materialIndex];
    vec3 albedo = mat.albedo.rgb;
    if (mat.base_color_texture >= 0) {
        albedo *= textureLod(textureArray[nonuniformEXT(mat.base_color_texture)], texCoord, 0.0).rgb;
    }

    vec3 N = worldNormal;
    if (mat.normal_map >= 0) {
        vec3 T = worldTangent;
        vec3 B = cross(N, T) * tangent.w;
        mat3 TBN = mat3(T, B, N);
        vec3 tangentNormal = textureLod(textureArray[nonuniformEXT(mat.normal_map)], texCoord, 0.0).xyz * 2.0 - 1.0;
        N = normalize(TBN * tangentNormal);
    }

    float metallic = mat.metallic;
    float roughness = mat.roughness;
    if (mat.metallic_roughness_map >= 0) {
        vec4 mrSample = textureLod(textureArray[nonuniformEXT(mat.metallic_roughness_map)], texCoord, 0.0);
        metallic *= mrSample.b;
        roughness *= mrSample.g;
    }

    vec3 V = normalize(cam.cameraPos.xyz - worldPos);
    vec3 L = normalize(-cam.directionalLight.direction.xyz);
    
    isShadowed = true; 
    traceRayEXT(topLevelAS, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT,
                0xFF, 0, 0, 1, worldPos + N * 0.001, 0.001, L, 10000.0, 1);

    vec3 radiance = cam.directionalLight.color.rgb * cam.directionalLight.intensity.x; 
    if (isShadowed) radiance = vec3(0.0);
    
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 directColor = CalculatePBR(N, V, L, albedo, roughness, metallic, F0, radiance);

    payload.hit = true;
    payload.color = directColor + mat.emission.rgb;
    payload.attenuation = FresnelSchlick(max(dot(N, V), 0.0), F0) * (1.0 - roughness);
    payload.rayOrigin = worldPos + N * 0.001;
    payload.rayDir = reflect(-V, N);
}