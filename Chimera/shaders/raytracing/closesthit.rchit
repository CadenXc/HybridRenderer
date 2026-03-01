#version 460
#extension GL_GOOGLE_include_directive : require
#include "../common/common.glsl"

layout(location = 0) rayPayloadInEXT HitPayload payload;
hitAttributeEXT vec2 attribs;

void main() 
{
    payload.hit = true;
    
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

    const vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    vec2 uv = v0.texCoord * barycentrics.x + v1.texCoord * barycentrics.y + v2.texCoord * barycentrics.z;

    // 1. 获取材质颜色
    vec4 albedo = GetAlbedo(mat, uv);
    vec3 vertexNormal = normalize(v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z);
    vec4 vertexTangent = v0.tangent * barycentrics.x + v1.tangent * barycentrics.y + v2.tangent * barycentrics.z;
    vec3 N = CalculateNormal(prim, mat, vertexNormal, vertexTangent, uv);

    float roughness = mat.roughness;
    float metallic = mat.metallic;
    if (mat.metalRoughTex >= 0) 
    {
        vec4 mrSample = texture(textureArray[nonuniformEXT(mat.metalRoughTex)], uv);
        roughness *= mrSample.g;
        metallic *= mrSample.b;
    }

    // 2. 准备 PBR 计算向量
    vec3 hitPos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    vec3 V = normalize(global.ubo.camera.position.xyz - hitPos);
    vec3 L = normalize(-global.ubo.sunLight.direction.xyz);
    vec3 lightColor = global.ubo.sunLight.color.rgb * global.ubo.sunLight.intensity.x;

    // 3. 执行直接光着色
    vec3 directLighting = EvaluateDirectPBR(N, V, L, albedo.rgb, roughness, metallic, lightColor);
    
    // [DEBUG] To find why white is black, let's include basic albedo in payload color for now
    // If you want pure PBR, use payload.color = directLighting;
    // But let's use: ambient + direct
    vec3 ambient = global.ubo.ambientStrength * albedo.rgb;
    payload.color = ambient + directLighting; 
    
    payload.normal = N;
    payload.distance = gl_HitTEXT;
    payload.roughness = roughness;
}
