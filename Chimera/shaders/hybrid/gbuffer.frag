#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common/common.glsl"

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in flat uint inObjectId;
layout(location = 3) in vec4 inCurPos;
layout(location = 4) in vec4 inPrevPos;
layout(location = 5) in vec4 inTangent;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outMaterial;
layout(location = 3) out vec4 outMotion; 
layout(location = 4) out vec4 outEmissive;

float LinearizeDepth(float d) 
{
    vec4 target = camera.projInverse * vec4(0.0, 0.0, d, 1.0);
    return abs(target.z / target.w);
}

void main() 
{
    GpuInstance inst = instances[inObjectId];
    GpuMaterial rawMat = materials[inst.material];
    
    // --- [PLAGIARISM] SVGF Material Point ---
    MaterialPoint mat = GetMaterialPoint(rawMat, inTexCoord);
    if (mat.Opacity < 0.1) discard;

    vec3 worldNormal = CalculateNormal(rawMat, normalize(inNormal), inTangent, inTexCoord);

    float safeCurW = abs(inCurPos.w) < 1e-6 ? 1e-6 : inCurPos.w;
    float safePrevW = abs(inPrevPos.w) < 1e-6 ? 1e-6 : inPrevPos.w;
    vec2 curUV  = (inCurPos.xy / safeCurW) * 0.5 + 0.5;
    vec2 prevUV = (inPrevPos.xy / safePrevW) * 0.5 + 0.5;
    
    float linearDepth = LinearizeDepth(gl_FragCoord.z);
    float dx = dFdx(linearDepth);
    float dy = dFdy(linearDepth);

    outMotion = vec4(curUV - prevUV, linearDepth, dx);
    outAlbedo = vec4(mat.Colour, dy); 

    outNormal = vec4(worldNormal, 1.0);
    outMaterial = vec4(mat.Roughness, mat.Metallic, 1.0, float(inObjectId)); // AO defaulted to 1.0
    outEmissive = vec4(mat.Emission, 1.0);
}
