#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require

struct HitPayload
{
    vec3 color;
    vec3 attenuation;
    vec3 rayOrigin;
    vec3 rayDir;
    bool hit;
    int  depth;
};

struct DirectionalLight { mat4 projview; vec4 direction; vec4 color; vec4 intensity; };

layout(location = 0) rayPayloadInEXT HitPayload payload;

layout(binding = 0, set = 0) uniform CameraProperties 
{
    mat4 view;
    mat4 proj;
    mat4 viewInverse;
    mat4 projInverse;
    mat4 viewProjInverse;
    mat4 prevView;
    mat4 prevProj;
    DirectionalLight directionalLight;
    vec2 displaySize;
    vec2 displaySizeInverse;
    uint frameIndex;
    uint frameCount;
    uint displayMode;
    vec4 cameraPos;
} cam;

layout(binding = 4, set = 1) uniform sampler2D textureArray[];

layout(push_constant) uniform PushConstants
{
    vec4 clearColor;
    vec3 lightPos;
    float lightIntensity;
    int frameCount;
    int skyboxIndex;
} pc;

const float PI = 3.14159265359;

void main() 
{
    payload.hit = false;

    if (pc.skyboxIndex >= 0) {
        vec3 d = normalize(gl_WorldRayDirectionEXT);
        // Equirectangular mapping
        float phi = atan(d.z, d.x);
        float theta = acos(d.y);
        vec2 uv = vec2(phi / (2.0 * PI) + 0.5, theta / PI);
        
        payload.color = textureLod(textureArray[nonuniformEXT(pc.skyboxIndex)], uv, 0.0).rgb;
    } else {
        payload.color = vec3(0.02, 0.02, 0.05); // Fallback color
    }
}