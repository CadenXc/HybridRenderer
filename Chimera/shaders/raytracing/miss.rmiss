#version 460
#extension GL_EXT_ray_tracing : require

// ======================= Payload 定义 =======================
// 必须与 RayGen 中的结构体完全一致
struct HitPayload
{
    vec3 color;
    vec3 normal;
    vec3 worldPos;
    int  depth;
};

layout(location = 0) rayPayloadInEXT HitPayload payload;

void main() 
{
    // 返回深蓝色背景
    payload.color = vec3(1.0, 1.0, 1.0);
    
    // 法线设为 0 (或者任何值，因为 depth=0 时 RayGen 不会用到它)
    payload.normal = vec3(0.0);
    payload.worldPos = vec3(0.0);
    
    // 标记：未击中物体
    payload.depth = 0; 
}