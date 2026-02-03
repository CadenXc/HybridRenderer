#version 450

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragPos;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 prevView;
    mat4 prevProj;
    vec4 lightPos;
    int frameCount;
} ubo;

void main() {
    // Current scene might not have valid textures yet, use a default color if needed
    // But we keep the texture sampling to see if it works
    vec3 color = vec3(0.8); 
    // vec3 color = texture(texSampler, fragTexCoord).rgb;
    
    // Ambient
    float ambientStrength = 0.3;
    vec3 ambient = ambientStrength * color;
    
    // Diffuse 
    vec3 norm = normalize(fragNormal);
    vec3 lightDir = normalize(ubo.lightPos.xyz - fragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * color;
    
    vec3 result = ambient + diffuse;
    outColor = vec4(result, 1.0);
}
