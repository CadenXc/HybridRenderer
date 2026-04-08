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
    GpuMaterial rawMat = materials[inst.material];

    // --- [PLAGIARISM] SVGF Material Point ---
    MaterialPoint mat = GetMaterialPoint(rawMat, inUV);
    vec3 worldNormal = CalculateNormal(rawMat, inNormal, inTangent, inUV);
    vec3 viewDirection = normalize(camera.position.xyz - inWorldPos);

    vec3 ddx = dFdx(inWorldPos);
    vec3 ddy = dFdy(inWorldPos);
    vec3 faceNormal = normalize(cross(ddx, ddy));
    if (dot(faceNormal, viewDirection) < 0.0) faceNormal = -faceNormal;

    vec3 lightDirection = normalize(-sunLight.direction.xyz);
    vec3 shadowOrigin = OffsetRay(inWorldPos, faceNormal);
    float shadow = CalculateRayQueryShadow(shadowOrigin, lightDirection, 10000.0);

    vec3 lightIntensity = sunLight.color.rgb * sunLight.intensity.x;
    
    // --- [PLAGIARISM] SVGF PBR Evaluation ---
    vec3 directLighting = EvalPbr(mat.Colour, 1.5, mat.Roughness, mat.Metallic, worldNormal, viewDirection, lightDirection) * lightIntensity;

    float ambStr = postData.y;
    vec3 ambient = ambStr * mat.Colour; 
    int skyboxIdx = int(envData.x);

    if (skyboxIdx >= 0)
    {
        vec3 reflectDirection = reflect(-viewDirection, worldNormal);
        vec3 envSpecular = texture(textureArray[nonuniformEXT(skyboxIdx)], SampleEquirectangular(reflectDirection)).rgb;
        vec3 envDiffuse = texture(textureArray[nonuniformEXT(skyboxIdx)], SampleEquirectangular(worldNormal)).rgb;
        vec3 F0 = mix(vec3(0.04), mat.Colour, mat.Metallic);
        vec3 F = FresnelSchlick(F0, worldNormal, viewDirection);
        vec3 kD = (vec3(1.0) - F) * (1.0 - mat.Metallic);
        ambient = (kD * envDiffuse * mat.Colour + F * envSpecular) * ambStr;
    }
    outColor = vec4(ambient + directLighting * shadow + mat.Emission, mat.Opacity);

    vec2 cur = (inCurPos.xy / inCurPos.w) * 0.5 + 0.5;
    vec2 prev = (inPrevPos.xy / inPrevPos.w) * 0.5 + 0.5;
    outMotion = cur - prev;
}
