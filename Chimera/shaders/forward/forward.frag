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
    // 1. 获取基础数据
    GpuPrimitive prim = primBuf.primitives[inObjectId];
    GpuMaterial mat = materialBuffer.m[prim.materialIndex];
    
    // 2. 解包材质参数 (Using common.glsl helpers)
    vec4 albedo = GetAlbedo(mat, inUV);
    vec3 N = CalculateNormal(mat, inNormal, inTangent, inUV);
    vec3 V = normalize(global.ubo.camera.position.xyz - inWorldPos);
    
    float roughness = mat.roughness;
    float metallic = mat.metallic;
    if (mat.metalRoughTex >= 0)
    {
        vec4 mrSample = texture(textureArray[nonuniformEXT(mat.metalRoughTex)], inUV);
        roughness *= mrSample.g;
        metallic *= mrSample.b;
    }

    // [NEW] 3. 获取 AO 和 Emissive
    float ao = GetAmbientOcclusion(mat, inUV);
    vec3 emissive = GetEmissive(mat, inUV);

    // 4. 计算直接光 (Sunlight)
    vec3 L = normalize(-global.ubo.sunLight.direction.xyz);
    vec3 lightColor = global.ubo.sunLight.color.rgb * global.ubo.sunLight.intensity.x;

    // [NEW] Ray Query Shadows
    vec3 ddx = dFdx(inWorldPos);
    vec3 ddy = dFdy(inWorldPos);
    vec3 faceNormal = normalize(cross(ddx, ddy));
    if (dot(faceNormal, V) < 0.0) 
    {
        faceNormal = -faceNormal;
    }
    
    vec3 shadowOrigin = OffsetRay(inWorldPos, faceNormal);
    float shadow = CalculateRayQueryShadow(shadowOrigin, L, 1000.0);

    vec3 directLighting = EvaluateDirectPBR(N, V, L, albedo.rgb, roughness, metallic, lightColor) * shadow;
    
    // 5. 环境光 (Basic IBL fallback)
    float ambStr = global.ubo.postData.y;
    int skyIdx = int(global.ubo.envData.x);

    vec3 ambient = ambStr * albedo.rgb * ao;
    if (skyIdx >= 0)
    {
        vec3 reflectDir = reflect(-V, N);
        // Simple IBL approximation: sample skybox for both diffuse and specular
        // In a real engine, we would use pre-filtered maps
        vec3 envSpecular = texture(textureArray[nonuniformEXT(skyIdx)], SampleEquirectangular(reflectDir)).rgb;
        vec3 envDiffuse = texture(textureArray[nonuniformEXT(skyIdx)], SampleEquirectangular(N)).rgb;
        
        vec3 F0 = mix(vec3(0.04), albedo.rgb, metallic);
        vec3 F = F_SchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
        
        ambient = (kD * envDiffuse * albedo.rgb + kS * envSpecular) * ao * ambStr;
    }
    
    // 6. 计算运动矢量
    vec2 curPos = (inCurPos.xy / inCurPos.w) * 0.5 + 0.5;
    vec2 prevPos = (inPrevPos.xy / inPrevPos.w) * 0.5 + 0.5;
    outMotionVector = curPos - prevPos;

    // 7. 最终合并
    outColor = vec4(ambient + directLighting + emissive, albedo.a);
}
