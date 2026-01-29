#version 450

layout(location = 0) in vec3 fragNormal;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 normal = normalize(fragNormal);
    outColor = vec4(normal * 0.5 + 0.5, 1.0);
}
