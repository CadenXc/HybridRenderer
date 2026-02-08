#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragPos;

layout(location = 0) out vec4 outColor;

struct DirectionalLight
{
    mat4 projview;
    vec4 direction;
    vec4 color;
    vec4 intensity;
};

// Set 0: Global UBO
layout(set = 0, binding = 0) uniform GlobalUBO {
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
} ubo;

// Set 1: Scene Resources
struct Material {
    vec4 albedo;
    vec4 emission;
    float roughness;
    float metallic;
    int albedoTex;
    int normalTex;
    int metalRoughTex;
    int padding[3];
};

layout(set = 1, binding = 0) readonly buffer MaterialBuffer {
    Material materials[];
} materialBuffer;

layout(set = 1, binding = 1) uniform sampler2D textureArray[];

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 normalMatrix;
    int materialIndex;
} push;

void main() {
    Material mat = materialBuffer.materials[push.materialIndex];
    
    vec4 albedo = mat.albedo;
    
    // index 0 is Magenta fallback.
    if (mat.albedoTex > 0) {
        albedo *= texture(textureArray[nonuniformEXT(mat.albedoTex)], fragTexCoord);
    } else if (mat.albedoTex == 0) {
        // Fallback texture detected
        albedo *= vec4(0.8, 0.5, 0.8, 1.0); 
    } else {
        // No texture (-1), use a neutral white if albedo is too dark, else keep albedo
        if (length(albedo.rgb) < 0.01) albedo.rgb = vec3(0.9);
    }

    // Alpha Cutoff
    if (albedo.a < 0.1) discard;

    vec3 norm = normalize(fragNormal);
    vec3 lightDir = normalize(-ubo.directionalLight.direction.xyz);
    if (dot(lightDir, lightDir) < 0.1) lightDir = normalize(vec3(0.5, 1.0, 0.5));

    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * albedo.rgb * ubo.directionalLight.color.rgb * ubo.directionalLight.intensity.x;
    vec3 ambient = 0.2 * albedo.rgb;
    
    vec3 viewDir = normalize(ubo.cameraPos.xyz - fragPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(norm, halfwayDir), 0.0), 32.0);
    vec3 specular = vec3(mat.metallic * 0.5) * spec;
    
    vec3 result = ambient + diffuse + specular + mat.emission.rgb;
    
    // Tonemapping & Gamma correction
    result = result / (result + vec3(1.0));
    result = pow(result, vec3(1.0/2.2));
    
    outColor = vec4(result, 1.0);
}