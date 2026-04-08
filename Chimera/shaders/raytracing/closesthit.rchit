#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common/common.glsl"

layout(location = 0) rayPayloadInEXT HitPayload payload;
hitAttributeEXT vec2 attribs;

void main() 
{
    uint objId = gl_InstanceCustomIndexEXT;
    GpuInstance inst = instances[objId];
    GpuMaterial rawMat = materials[inst.material];
    
    IndexBufferRef indices = IndexBufferRef(inst.indexAddress);
    uint i0 = indices.i[3 * gl_PrimitiveID + 0];
    uint i1 = indices.i[3 * gl_PrimitiveID + 1];
    uint i2 = indices.i[3 * gl_PrimitiveID + 2];

    VertexBufferRef vertices = VertexBufferRef(inst.vertexAddress);
    GpuVertex v0 = vertices.v[i0];
    GpuVertex v1 = vertices.v[i1];
    GpuVertex v2 = vertices.v[i2];

    const vec3 bary = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    
    vec3 localPos = v0.pos * bary.x + v1.pos * bary.y + v2.pos * bary.z;
    vec2 uv = v0.texCoord * bary.x + v1.texCoord * bary.y + v2.texCoord * bary.z;
    vec3 localNormal = normalize(v0.normal * bary.x + v1.normal * bary.y + v2.normal * bary.z);
    vec4 localTangent = v0.tangent * bary.x + v1.tangent * bary.y + v2.tangent * bary.z;
    
    vec3 worldPos = (inst.transform * vec4(localPos, 1.0)).xyz;
    mat3 normalMat = mat3(inst.normalTransform);
    vec3 geoNormal = normalize(normalMat * localNormal);
    vec4 worldTangent = vec4(normalize(normalMat * localTangent.xyz), localTangent.w);

    if (dot(geoNormal, gl_WorldRayDirectionEXT) > 0.0) geoNormal = -geoNormal;

    // --- [PLAGIARISM] SVGF Material Point Extraction ---
    MaterialPoint mat = GetMaterialPoint(rawMat, uv);
    vec3 worldNormal = CalculateNormal(rawMat, geoNormal, worldTangent, uv);

    // --- Lighting Calculation ---
    uint renderFlags = frameData.w;
    bool lightEnabled = (renderFlags & RENDER_FLAG_LIGHT_BIT) != 0;
    vec3 viewDir = -gl_WorldRayDirectionEXT; 
    
    // 1. Sun Light (Existing)
    vec3 sunDir = normalize(-sunLight.direction.xyz);
    vec3 sunIntensity = lightEnabled ? (sunLight.color.rgb * sunLight.intensity.x) : vec3(0.0);
    vec3 shadowOrigin = OffsetRay(worldPos, geoNormal);
    float sunShadow = CalculateRayQueryShadow(shadowOrigin, sunDir, 1000.0);
    vec3 directLighting = EvalPbr(mat.Colour, 1.5, mat.Roughness, mat.Metallic, worldNormal, viewDir, sunDir) * sunShadow * sunIntensity;

    // 2. [NEW] Light Importance Sampling (NEE)
    uint seed = InitRandomSeed(gl_LaunchIDEXT.x + gl_LaunchIDEXT.y * gl_LaunchSizeEXT.x, frameData.x);
    int sampledInst = INVALID_ID;
    vec3 sampledLightDir = SampleLights(worldPos, RandomFloat(seed), RandomFloat(seed), vec2(RandomFloat(seed), RandomFloat(seed)), sampledInst);
    
    if (length(sampledLightDir) > 0.001) {
        float shadow = CalculateRayQueryShadow(shadowOrigin, sampledLightDir, 1000.0);
        // Simplified NEE: Just add radiance if visible. 
        // A full MIS would require PDFs from both BSDF and Light.
        if (shadow > 0.5) {
            if (sampledInst != INVALID_ID) {
                GpuInstance sInst = instances[sampledInst];
                GpuMaterial sMat = materials[sInst.material];
                vec3 lightRadiance = sMat.emission * 5.0; // Scaled emission
                directLighting += EvalPbr(mat.Colour, 1.5, mat.Roughness, mat.Metallic, worldNormal, viewDir, sampledLightDir) * lightRadiance;
            } else {
                // Environment light radiance
                int skyIdx = int(envData.x);
                if (skyIdx >= 0) {
                    vec3 envRadiance = texture(textureArray[nonuniformEXT(skyIdx)], SampleEquirectangular(sampledLightDir)).rgb * postData.y;
                    directLighting += EvalPbr(mat.Colour, 1.5, mat.Roughness, mat.Metallic, worldNormal, viewDir, sampledLightDir) * envRadiance;
                }
            }
        }
    }

    vec3 ambient = vec3(0.0);
    int skyIdx = int(envData.x);
    if (skyIdx >= 0 && (renderFlags & RENDER_FLAG_IBL_BIT) != 0)
    {
        vec3 R = reflect(-viewDir, worldNormal);
        vec3 envSpecular = texture(textureArray[nonuniformEXT(skyIdx)], SampleEquirectangular(R)).rgb;
        vec3 envDiffuse = texture(textureArray[nonuniformEXT(skyIdx)], SampleEquirectangular(worldNormal)).rgb;

        vec3 F0 = mix(vec3(0.04), mat.Colour, mat.Metallic);
        vec3 F = FresnelSchlick(F0, worldNormal, viewDir);
        vec3 kD = (vec3(1.0) - F) * (1.0 - mat.Metallic);
        float ambientIntensity = max(postData.y, 0.2); 
        ambient = (kD * envDiffuse * mat.Colour + F * envSpecular) * ambientIntensity;
    }

    vec4 clipPos = WorldToClip(vec4(worldPos, 1.0));
    vec4 prevClipPos = PrevWorldToClip(LocalToWorld(localPos, inst.prevTransform));
    vec2 motion = (clipPos.xy / clipPos.w * 0.5 + 0.5) - (prevClipPos.xy / prevClipPos.w * 0.5 + 0.5);

    vec3 totalRadiance = directLighting + ambient + mat.Emission;
    
    payload.color_dist = vec4(totalRadiance, gl_HitTEXT);
    payload.normal_rough = vec4(worldNormal, mat.Roughness);
    payload.motion_hit = vec4(motion, 1.0, 0.0);
}
