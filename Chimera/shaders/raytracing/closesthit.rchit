#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common/common.glsl"

layout(location = 0) rayPayloadInEXT HitPayload payload;
hitAttributeEXT vec2 attribs;

void main() 
{
    // 1. Fetch Geometry Data
    uint objId = gl_InstanceCustomIndexEXT;
    GpuPrimitive prim = primitives[objId];
    GpuMaterial mat = materials[prim.materialIndex];
    
    // Fetch Indices and Vertices
    IndexBufferRef indices = IndexBufferRef(prim.indexAddress);
    uint i0 = indices.i[3 * gl_PrimitiveID + 0];
    uint i1 = indices.i[3 * gl_PrimitiveID + 1];
    uint i2 = indices.i[3 * gl_PrimitiveID + 2];

    VertexBufferRef vertices = VertexBufferRef(prim.vertexAddress);
    GpuVertex v0 = vertices.v[i0];
    GpuVertex v1 = vertices.v[i1];
    GpuVertex v2 = vertices.v[i2];

    const vec3 bary = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    
    // Interpolate Attributes
    vec3 localPos = v0.pos * bary.x + v1.pos * bary.y + v2.pos * bary.z;
    vec2 uv = v0.texCoord * bary.x + v1.texCoord * bary.y + v2.texCoord * bary.z;
    vec3 localNormal = normalize(v0.normal * bary.x + v1.normal * bary.y + v2.normal * bary.z);
    vec4 localTangent = v0.tangent * bary.x + v1.tangent * bary.y + v2.tangent * bary.z;
    
    // 2. Transform to World Space
    vec3 worldPos = (prim.transform * vec4(localPos, 1.0)).xyz;
    mat3 normalMat = mat3(prim.normalMatrix);
    vec3 geoNormal = normalize(normalMat * localNormal);
    vec4 worldTangent = vec4(normalize(normalMat * localTangent.xyz), localTangent.w);

    // Orientation Check (Backface handling)
    if (dot(geoNormal, gl_WorldRayDirectionEXT) > 0.0) geoNormal = -geoNormal;

    // 3. Material Properties
    vec4 albedoSample = GetAlbedo(mat, uv);
    vec3 worldNormal = CalculateNormal(mat, geoNormal, worldTangent, uv);

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

    // 4. Lighting Calculation
    vec3 viewDir = -gl_WorldRayDirectionEXT; 
    vec3 lightDir = normalize(-sunLight.direction.xyz);
    vec3 lightIntensity = sunLight.color.rgb * sunLight.intensity.x;

    // Ray Query Shadows from Hit Point
    vec3 shadowOrigin = OffsetRay(worldPos, geoNormal);
    float shadow = CalculateRayQueryShadow(shadowOrigin, lightDir, 1000.0);

    // Direct PBR
    vec3 directLighting = EvaluateDirectPBR(worldNormal, viewDir, lightDir, albedoSample.rgb, roughness, metallic, lightIntensity) * shadow;

    // Indirect IBL Fallback
    vec3 ambient = vec3(0.0);
    int skyIdx = int(envData.x);
    if (skyIdx >= 0)
    {
        vec3 R = reflect(-viewDir, worldNormal);
        vec3 envSpecular = texture(textureArray[nonuniformEXT(skyIdx)], SampleEquirectangular(R)).rgb;
        vec3 envDiffuse = texture(textureArray[nonuniformEXT(skyIdx)], SampleEquirectangular(worldNormal)).rgb;

        vec3 F0 = mix(vec3(0.04), albedoSample.rgb, metallic);
        vec3 F = FresnelSchlickRoughness(max(dot(worldNormal, viewDir), 0.0), F0, roughness);
        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

        ambient = (kD * envDiffuse * albedoSample.rgb + F * envSpecular) * ao * postData.y;
    }

    // 5. Motion Vectors (Strictly Unjittered)
    vec4 clipPos = WorldToClip(vec4(worldPos, 1.0));
    vec4 prevClipPos = PrevWorldToClip(LocalToWorld(localPos, prim.prevTransform));
    vec2 motion = (clipPos.xy / clipPos.w * 0.5 + 0.5) - (prevClipPos.xy / prevClipPos.w * 0.5 + 0.5);

    // 6. Final Payload Assignment
    payload.color_dist = vec4(directLighting + ambient + emissive, gl_HitTEXT);
    payload.normal_rough = vec4(worldNormal, roughness);
    payload.motion_hit = vec4(motion, 1.0, 0.0);
}
