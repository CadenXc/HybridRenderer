#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common/common.glsl"

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inWorldPos;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in flat uint inObjectId; 
layout(location = 5) in vec4 inCurPos;
layout(location = 6) in vec4 inPrevPos;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outMotion;

void main() 
{
    GpuInstance inst = instances[inObjectId];
    GpuMaterial mat = materials[inst.material];

    vec4 albedoSample = GetAlbedo(mat, inUV);
    vec3 baseColor = albedoSample.rgb;
    vec3 worldNormal = CalculateNormal(mat, inNormal, inTangent, inUV);
    vec3 viewDirection = normalize(camera.position.xyz - inWorldPos);

    float roughness = mat.roughness;
    float metallic = mat.metallic;
    if (mat.roughnessTexture >= 0)
    {
        vec4 mrSample = texture(textureArray[nonuniformEXT(mat.roughnessTexture)], inUV);
        roughness *= mrSample.g;
        metallic *= mrSample.b;
    }

    float ao = GetAmbientOcclusion(mat, inUV);
    vec3 emissive = GetEmissive(mat, inUV);

    vec3 ddx = dFdx(inWorldPos);
    vec3 ddy = dFdy(inWorldPos);
    vec3 faceNormal = normalize(cross(ddx, ddy));
    if (dot(faceNormal, viewDirection) < 0.0) faceNormal = -faceNormal;

    vec3 lightDirection = normalize(-sunLight.direction.xyz);
    vec3 shadowOrigin = OffsetRay(inWorldPos, faceNormal);
    float shadow = CalculateRayQueryShadow(shadowOrigin, lightDirection, 10000.0);

    vec3 lightIntensity = sunLight.color.rgb * sunLight.intensity.x;
    vec3 directLighting = EvaluateDirectPBR(worldNormal, viewDirection, lightDirection, baseColor, roughness, metallic, lightIntensity);

    vec3 ambient = postData.y * baseColor * ao; 
    int skyboxIdx = int(envData.x);

    if (skyboxIdx >= 0)
    {
        vec3 reflectDirection = reflect(-viewDirection, worldNormal);
        vec3 envSpecular = texture(textureArray[nonuniformEXT(skyboxIdx)], SampleEquirectangular(reflectDirection)).rgb;
        vec3 envDiffuse = texture(textureArray[nonuniformEXT(skyboxIdx)], SampleEquirectangular(worldNormal)).rgb;
        vec3 F0 = mix(vec3(0.04), baseColor, metallic);
        vec3 fresnelTerm = FresnelSchlickRoughness(max(dot(worldNormal, viewDirection), 0.0), F0, roughness);
        vec3 kS = fresnelTerm;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
        ambient = (kD * envDiffuse * baseColor + kS * envSpecular) * ao * postData.y;
    }
    outColor = vec4(ambient + directLighting * shadow + emissive, albedoSample.a);

    vec2 cur = (inCurPos.xy / inCurPos.w) * 0.5 + 0.5;
    vec2 prev = (inPrevPos.xy / inPrevPos.w) * 0.5 + 0.5;
    outMotion = cur - prev;
}
