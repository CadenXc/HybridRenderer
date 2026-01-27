#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;

// G-Buffer Outputs
layout(location = 0) out vec4 outAlbedo;   // RGBA8
layout(location = 1) out vec4 outNormal;   // RGBA16F (XYZ + unused)
layout(location = 2) out vec4 outMaterial; // RGBA8 (Roughness, Metallic, etc.)

// Bindings (Standard Scene Descriptor Set)
layout(set = 0, binding = 1) uniform sampler2D texSampler;

void main() {
    // Albedo
    outAlbedo = texture(texSampler, fragTexCoord);
    
    // Normal (Store directly as float)
    outNormal = vec4(normalize(fragNormal), 1.0);

    // Material (Placeholder)
    // R = Roughness, G = Metallic
    outMaterial = vec4(0.5, 0.0, 0.0, 1.0); 
}
