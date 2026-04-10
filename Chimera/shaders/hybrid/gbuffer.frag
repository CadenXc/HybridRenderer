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
layout(location = 2) out vec4 outMaterialParams;
layout(location = 3) out uint outObjectID;
layout(location = 4) out vec4 outMotion; 
layout(location = 5) out vec4 outEmissive;

float LinearizeDepth(float d) 
{
    vec4 target = camera.projInverse * vec4(0.0, 0.0, d, 1.0);
    return abs(target.z / target.w);
}

void main() 
{
    // 获取基础材质和实例数据
    GpuInstance inst = instances[inObjectId];
    GpuMaterial rawMat = materials[inst.material];
    MaterialPoint mat = GetMaterialPoint(rawMat, inTexCoord);
    
    // Alpha 测试：早期剔除透明像素
    if (mat.Opacity < 0.1) discard;

    // --- 准备公用计算变量 ---
    float linearDepth = LinearizeDepth(gl_FragCoord.z);
    float dx = dFdx(linearDepth);
    float dy = dFdy(linearDepth);

    // [Location 0]: Albedo (RGB) + 垂直深度梯度 (A)
    outAlbedo = vec4(mat.Colour, dy); 

    // [Location 1]: 世界法线
    vec3 worldNormal = CalculateNormal(rawMat, normalize(inNormal), inTangent, inTexCoord);
    outNormal = vec4(worldNormal, 1.0);

    // [Location 2]: PBR 参数
    // R: 粗糙度, G: 金属度, B: 预留AO, A: 材质类型编码
    float ao = 1.0; 
    float shadingModelID = float(mat.MaterialType) / 255.0;
    outMaterialParams = vec4(mat.Roughness, mat.Metallic, ao, shadingModelID);

    // [Location 3]: 原始物体 ID (UINT)
    outObjectID = inObjectId;

    // [Location 4]: 运动矢量计算
    float safeCurW = abs(inCurPos.w) < 1e-6 ? 1e-6 : inCurPos.w;
    float safePrevW = abs(inPrevPos.w) < 1e-6 ? 1e-6 : inPrevPos.w;
    vec2 curUV  = (inCurPos.xy / safeCurW) * 0.5 + 0.5;
    vec2 prevUV = (inPrevPos.xy / safePrevW) * 0.5 + 0.5;
    
    // RG: 运动矢量, B: 线性深度, A: 水平深度梯度
    outMotion = vec4(curUV - prevUV, linearDepth, dx);

    // [Location 5]: 自发光
    outEmissive = vec4(mat.Emission, 1.0);
}
