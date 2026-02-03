#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec4 fragCurrPos;
layout(location = 3) in vec4 fragPrevPos;
layout(location = 4) in flat int inMaterialIndex;

layout(location = 0) out vec4 outAlbedo;   // RT0
layout(location = 1) out vec4 outNormal;   // RT1
layout(location = 2) out vec4 outMaterial; // RT2
layout(location = 3) out vec4 outMotion;   // RT3

void main() {
    // RT0: Albedo
    // TODO: Sample texture using inMaterialIndex
    outAlbedo = vec4(0.8, 0.8, 0.8, 1.0); 

    // RT1: Normal (World Space)
    outNormal = vec4(normalize(fragNormal) * 0.5 + 0.5, 1.0);

    // RT2: Material (Roughness, Metallic, AO, Object ID)
    // We store Material Index in Alpha channel for now
    float roughness = 0.5;
    float metallic = 0.0;
    outMaterial = vec4(roughness, metallic, 1.0, float(inMaterialIndex));

    // RT3: Motion Vectors
    vec2 curr = (fragCurrPos.xy / fragCurrPos.w) * 0.5 + 0.5;
    vec2 prev = (fragPrevPos.xy / fragPrevPos.w) * 0.5 + 0.5;
    outMotion = vec4(curr - prev, 0.0, 1.0);
}