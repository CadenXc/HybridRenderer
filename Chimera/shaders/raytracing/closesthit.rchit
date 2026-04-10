#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common/common.glsl"

/**
 * @file closesthit.rchit
 * @brief Ray Tracing Closest Hit Shader (光线追踪最近击中着色器)
 * 
 * 当射线撞击场景中的最近几何体时触发。它是混合渲染中负责“计算材质和光照”的核心阶段。
 * 
 * 主要职责 (Responsibilities):
 * 1. 几何插值 (Geometry Interpolation): 手动利用重心坐标插值顶点数据。
 * 2. 材质评估 (Material Evaluation): 处理纹理映射、PBR 参数（Albedo, Metallic, Roughness）。
 * 3. 直接光照 (Direct Lighting): 利用 Next Event Estimation (NEE) 评估太阳光及发光面光源。
 * 4. 间接光照 (Indirect Lighting): 计算基于环境贴图（IBL）的背景贡献。
 * 5. 运动矢量 (Motion Vectors): 计算当前点在上一帧的屏幕位置，支持 TAA 和降噪器重投影。
 */

layout(location = 0) rayPayloadInEXT HitPayload payload;
hitAttributeEXT vec2 attribs; // 由 GPU 硬件自动生成的重心坐标属性

void main() 
{
    // --- 1. 几何重建 (Geometry Reconstruction) ---
    // 每一个实例（Mesh）都拥有独立的偏移和地址
    uint objId = gl_InstanceCustomIndexEXT;
    GpuInstance inst = instances[objId];
    GpuMaterial rawMat = materials[inst.material];
    
    // 获取当前三角形的三个顶点索引
    IndexBufferRef indices = IndexBufferRef(inst.indexAddress);
    uint i0 = indices.i[3 * gl_PrimitiveID + 0];
    uint i1 = indices.i[3 * gl_PrimitiveID + 1];
    uint i2 = indices.i[3 * gl_PrimitiveID + 2];

    // 从顶点缓冲区中提取原始数据
    VertexBufferRef vertices = VertexBufferRef(inst.vertexAddress);
    GpuVertex v0 = vertices.v[i0];
    GpuVertex v1 = vertices.v[i1];
    GpuVertex v2 = vertices.v[i2];

    // 利用重心坐标对顶点属性进行线性插值
    const vec3 bary = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    
    vec3 localPos = v0.pos * bary.x + v1.pos * bary.y + v2.pos * bary.z;
    vec2 uv = v0.texCoord * bary.x + v1.texCoord * bary.y + v2.texCoord * bary.z;
    vec3 localNormal = normalize(v0.normal * bary.x + v1.normal * bary.y + v2.normal * bary.z);
    vec4 localTangent = v0.tangent * bary.x + v1.tangent * bary.y + v2.tangent * bary.z;
    
    // 将局部空间坐标和法线转换至世界空间
    vec3 worldPos = (inst.transform * vec4(localPos, 1.0)).xyz;
    mat3 normalMat = mat3(inst.normalTransform);
    vec3 geoNormal = normalize(normalMat * localNormal);
    vec4 worldTangent = vec4(normalize(normalMat * localTangent.xyz), localTangent.w);

    // [FIX] 正确处理反向面（Back-facing）的法线朝向，防止光照异常
    if (dot(geoNormal, gl_WorldRayDirectionEXT) > 0.0) geoNormal = -geoNormal;

    // --- 2. 材质采样 (Material Fetching) ---
    // 应用纹理贴图，并结合法线贴图计算世界空间的最终法线（worldNormal）
    MaterialPoint mat = GetMaterialPoint(rawMat, uv);
    vec3 worldNormal = CalculateNormal(rawMat, geoNormal, worldTangent, uv);

    // --- 3. 直接光照计算 (Direct Lighting) ---
    uint renderFlags = frameData.w;
    bool lightEnabled = (renderFlags & RENDER_FLAG_LIGHT_BIT) != 0;
    vec3 viewDir = -gl_WorldRayDirectionEXT; // 反向射线方向即为视点方向
    
    // A. 太阳光 (Directional Sun Light)
    vec3 sunDir = normalize(-sunLight.direction.xyz);
    vec3 sunIntensity = lightEnabled ? (sunLight.color.rgb * sunLight.intensity.x) : vec3(0.0);
    vec3 shadowOrigin = OffsetRay(worldPos, geoNormal);
    
    // 阴影检测：在 Closest Hit 内部调用 Ray Query (Inline) 以避免递归管线开销
    float sunShadow = CalculateRayQueryShadow(shadowOrigin, sunDir, 1000.0);
    vec3 directLighting = EvalPbr(mat.Colour, 1.5, mat.Roughness, mat.Metallic, worldNormal, viewDir, sunDir) * sunShadow * sunIntensity;

    // B. 面光源采样 (Next Event Estimation - Emissive Area Lights)
    // 根据 CDF 随机采样场景中的发光体。
    uint seed = InitRandomSeed(gl_LaunchIDEXT.x + gl_LaunchIDEXT.y * gl_LaunchSizeEXT.x, frameData.x);
    int sampledInst = INVALID_ID;
    vec3 sampledLightDir = SampleLights(worldPos, RandomFloat(seed), RandomFloat(seed), vec2(RandomFloat(seed), RandomFloat(seed)), sampledInst);
    
    if (length(sampledLightDir) > 0.001) {
        float shadow = CalculateRayQueryShadow(shadowOrigin, sampledLightDir, 1000.0);
        if (shadow > 0.5) {
            if (sampledInst != INVALID_ID) {
                GpuInstance sInst = instances[sampledInst];
                GpuMaterial sMat = materials[sInst.material];
                // 采样发光材质并应用 PBR 评估
                vec3 lightRadiance = sMat.emission * 5.0; 
                directLighting += EvalPbr(mat.Colour, 1.5, mat.Roughness, mat.Metallic, worldNormal, viewDir, sampledLightDir) * lightRadiance;
            }
        }
    }

    // --- 4. 间接光照/环境光 (Indirect Lighting - IBL) ---
    vec3 ambient = vec3(0.0);
    int skyIdx = int(envData.x);
    if (skyIdx >= 0 && (renderFlags & RENDER_FLAG_IBL_BIT) != 0)
    {
        // 简单评估：采样环境图（天空盒）作为漫反射和镜面反射的背景来源
        vec3 R = reflect(-viewDir, worldNormal);
        vec3 envSpecular = texture(textureArray[nonuniformEXT(skyIdx)], SampleEquirectangular(R)).rgb;
        vec3 envDiffuse = texture(textureArray[nonuniformEXT(skyIdx)], SampleEquirectangular(worldNormal)).rgb;

        vec3 F0 = mix(vec3(0.04), mat.Colour, mat.Metallic);
        vec3 F = FresnelSchlick(F0, worldNormal, viewDir);
        vec3 kD = (vec3(1.0) - F) * (1.0 - mat.Metallic);
        
        // 合并环境漫反射和镜面反射
        ambient = (kD * envDiffuse * mat.Colour + F * envSpecular) * max(postData.y, 0.2); 
    }

    // --- 5. 运动矢量计算 (Motion Vectors) ---
    // 将当前物体在上一帧的位置通过变换还原至上一帧屏幕坐标，以实现降噪算法的重投影（Reprojection）
    vec4 clipPos = WorldToClip(vec4(worldPos, 1.0));
    vec4 prevClipPos = PrevWorldToClip(LocalToWorld(localPos, inst.prevTransform));
    vec2 motion = (clipPos.xy / clipPos.w * 0.5 + 0.5) - (prevClipPos.xy / prevClipPos.w * 0.5 + 0.5);

    // 最终颜色合成
    vec3 totalRadiance = directLighting + ambient + mat.Emission;
    
    // 将计算好的结果填回 Ray Payload 中，供 Raygen 阶段最终收集
    payload.color_dist = vec4(totalRadiance, gl_HitTEXT); // RGB + 命中距离
    payload.normal_rough = vec4(worldNormal, mat.Roughness);
    payload.motion_hit = vec4(motion, 1.0, 0.0);
}