#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common/common.glsl"

layout(location = 0) rayPayloadInEXT HitPayload payload;
hitAttributeEXT vec2 attribs;

void main() 
{
    uint objId = gl_InstanceCustomIndexEXT;
    GpuPrimitive prim = primBuf.primitives[objId];
    GpuMaterial mat = materialBuffer.m[prim.materialIndex];
    
    IndexBufferRef indices = IndexBufferRef(prim.indexAddress);
    uint i0 = indices.i[3 * gl_PrimitiveID];
    uint i1 = indices.i[3 * gl_PrimitiveID + 1];
    uint i2 = indices.i[3 * gl_PrimitiveID + 2];

    VertexBufferRef vertices = VertexBufferRef(prim.vertexAddress);
    GpuVertex v0 = vertices.v[i0];
    GpuVertex v1 = vertices.v[i1];
    GpuVertex v2 = vertices.v[i2];

    vec3 wPos0 = vec3(prim.transform * vec4(v0.pos, 1.0));
    vec3 wPos1 = vec3(prim.transform * vec4(v1.pos, 1.0));
    vec3 wPos2 = vec3(prim.transform * vec4(v2.pos, 1.0));
    vec3 faceNormal = normalize(cross(wPos1 - wPos0, wPos2 - wPos0));

    const vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    vec2 uv = v0.texCoord * barycentrics.x + v1.texCoord * barycentrics.y + v2.texCoord * barycentrics.z;

    vec3 localNormal = normalize(v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z);
    vec4 localTangent = v0.tangent * barycentrics.x + v1.tangent * barycentrics.y + v2.tangent * barycentrics.z;
    
    mat3 normalMat = mat3(prim.normalMatrix);
    vec3 worldNormal = normalize(normalMat * localNormal);
    vec4 worldTangent = vec4(normalize(normalMat * localTangent.xyz), localTangent.w);

    if (dot(faceNormal, gl_WorldRayDirectionEXT) > 0.0) faceNormal = -faceNormal;
    if (dot(worldNormal, gl_WorldRayDirectionEXT) > 0.0) worldNormal = -worldNormal;

    vec4 albedo = GetAlbedo(mat, uv);
    vec3 N = CalculateNormal(mat, worldNormal, worldTangent, uv);

    float roughness = mat.roughness;
    float metallic = mat.metallic;
    if (mat.metalRoughTex >= 0) 
    {
        vec4 mrSample = texture(textureArray[nonuniformEXT(mat.metalRoughTex)], uv);
        roughness *= mrSample.g;
        metallic *= mrSample.b;
    }

    float ao = GetAmbientOcclusion(mat, uv);
    vec3 emissive = GetEmissive(mat, uv);

    vec3 hitPos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    vec3 V = -gl_WorldRayDirectionEXT; 
    vec3 L = normalize(-global.ubo.sunLight.direction.xyz);
    vec3 lightIntensity = global.ubo.sunLight.color.rgb * global.ubo.sunLight.intensity.x;

    // Restore Shadow with safe offset
    vec3 shadowOrigin = OffsetRay(hitPos, faceNormal);
    float shadow = CalculateRayQueryShadow(shadowOrigin, L, 1000.0);

    vec3 directLighting = EvaluateDirectPBR(N, V, L, albedo.rgb, roughness, metallic, lightIntensity) * shadow;

    // Restore IBL Fallback with procedural sky logic sync
    vec3 ambient = global.ubo.postData.y * albedo.rgb * ao;
    int skyIdx = int(global.ubo.envData.x);
    if (skyIdx >= 0)
    {
        vec3 reflectDir = reflect(-V, N);
        vec3 envSpecular = texture(textureArray[nonuniformEXT(skyIdx)], SampleEquirectangular(reflectDir)).rgb;
        vec3 envDiffuse = texture(textureArray[nonuniformEXT(skyIdx)], SampleEquirectangular(N)).rgb;

        vec3 F0 = mix(vec3(0.04), albedo.rgb, metallic);
        vec3 F = F_SchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

        ambient = (kD * envDiffuse * albedo.rgb + kS * envSpecular) * ao * global.ubo.postData.y;
    }
    else
    {
        // Simple IBL fallback matching procedural sky in miss shader
        float t = 0.5 * (N.y + 1.0);
        vec3 envDiffuse = mix(vec3(0.4, 0.5, 0.6), vec3(0.1, 0.2, 0.4), t);
        ambient = envDiffuse * albedo.rgb * ao * global.ubo.postData.y * 0.2;
    }

    vec3 finalColor = ambient + directLighting + emissive;

    // Calculate motion vectors for TAA/SVGF
    vec3 localPos = v0.pos * barycentrics.x + v1.pos * barycentrics.y + v2.pos * barycentrics.z;
    vec4 curPos = ProjectPosition(localPos, prim.transform);
    vec4 prevPos = ProjectPreviousPosition(localPos, prim.prevTransform);
    vec2 motion = (curPos.xy / curPos.w * 0.5 + 0.5) - (prevPos.xy / prevPos.w * 0.5 + 0.5);

    payload.color_dist = vec4(finalColor, gl_HitTEXT);
    payload.normal_rough = vec4(N, roughness);
    payload.motion_hit = vec4(motion, 1.0, 0.0);
}
