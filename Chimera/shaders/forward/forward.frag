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
layout(location = 1) out vec2 outMotionVector;

void main() 
{
    GpuInstance inst = instances[inObjectId];
    GpuMaterial rawMat = materials[inst.material];
    
    // --- [PLAGIARISM] SVGF Material Point ---
    MaterialPoint mat = GetMaterialPoint(rawMat, inUV);
    vec3 worldNormal = CalculateNormal(rawMat, inNormal, inTangent, inUV);
    vec3 viewDirection = normalize(camera.position.xyz - inWorldPos);
    
    uint renderFlags = frameData.w;
    bool lightEnabled = (renderFlags & RENDER_FLAG_LIGHT_BIT) != 0;
    vec3 lightDirection = normalize(-sunLight.direction.xyz);
    vec3 lightIntensity = lightEnabled ? (sunLight.color.rgb * sunLight.intensity.x) : vec3(0.0);

    vec3 ddx = dFdx(inWorldPos);
    vec3 ddy = dFdy(inWorldPos);
    vec3 faceNormal = normalize(cross(ddx, ddy));
    if (dot(faceNormal, viewDirection) < 0.0) faceNormal = -faceNormal;
    
    vec3 shadowOrigin = OffsetRay(inWorldPos, faceNormal);
    float shadow = CalculateRayQueryShadow(shadowOrigin, lightDirection, 1000.0);

    // --- [PLAGIARISM] SVGF PBR Evaluation ---
    vec3 directLighting = EvalPbr(mat.Colour, 1.5, mat.Roughness, mat.Metallic, worldNormal, viewDirection, lightDirection) * shadow * lightIntensity;
    
    float ambStr = postData.y;
    int skyIdx = int(envData.x);
    vec3 ambient = ambStr * mat.Colour; // Base ambient fallback
    if (skyIdx >= 0)
    {
        vec3 reflectDirection = reflect(-viewDirection, worldNormal);
        vec3 envSpecular = texture(textureArray[nonuniformEXT(skyIdx)], SampleEquirectangular(reflectDirection)).rgb;
        vec3 envDiffuse = texture(textureArray[nonuniformEXT(skyIdx)], SampleEquirectangular(worldNormal)).rgb;
        
        vec3 F0 = mix(vec3(0.04), mat.Colour, mat.Metallic);
        vec3 F = FresnelSchlick(F0, worldNormal, viewDirection);
        vec3 kD = (vec3(1.0) - F) * (1.0 - mat.Metallic);
        ambient = (kD * envDiffuse * mat.Colour + F * envSpecular) * ambStr;
    }
    
    float safeCurW = abs(inCurPos.w) < 1e-6 ? 1e-6 : inCurPos.w;
    float safePrevW = abs(inPrevPos.w) < 1e-6 ? 1e-6 : inPrevPos.w;
    vec2 curPos = (inCurPos.xy / safeCurW) * 0.5 + 0.5;
    vec2 prevPos = (inPrevPos.xy / safePrevW) * 0.5 + 0.5;
    outMotionVector = (curPos - prevPos);

    vec3 color = ambient + directLighting + mat.Emission;
    
    uint displayMode = frameData.z;
    if (displayMode == DISPLAY_MODE_ALBEDO)   { outColor = vec4(mat.Colour, 1.0); return; }
    if (displayMode == DISPLAY_MODE_NORMAL)   { outColor = vec4(worldNormal * 0.5 + 0.5, 1.0); return; }
    if (displayMode == DISPLAY_MODE_MATERIAL) { outColor = vec4(mat.Roughness, mat.Metallic, 1.0, 1.0); return; }
    if (displayMode == DISPLAY_MODE_MOTION)   { outColor = vec4(abs(outMotionVector) * 100.0, 0.0, 1.0); return; }
    if (displayMode == DISPLAY_MODE_DEPTH)    { float depth = gl_FragCoord.z; outColor = vec4(vec3(depth), 1.0); return; }

    outColor = vec4(color, mat.Opacity);
}
