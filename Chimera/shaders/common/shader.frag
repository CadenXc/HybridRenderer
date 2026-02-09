#version 450
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : require

#include "ShaderCommon.h"

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragPos;

layout(location = 0) out vec4 outColor;

// Set 0: Global
layout(set = 0, binding = 0) uniform GlobalUBO {
    UniformBufferObject cam;
} ubo;

// Set 1: Scene (Standardized)
layout(set = 1, binding = 1) readonly buffer MaterialBuffer { PBRMaterial materials[]; } materialBuffer;
layout(set = 1, binding = 3) uniform sampler2D textureArray[];

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 normalMatrix;
    int materialIndex;
} push;

void main() {
    PBRMaterial mat = materialBuffer.materials[push.materialIndex];
    vec4 baseColor = mat.albedo;
    if (mat.albedoTex >= 0) {
        baseColor *= texture(textureArray[nonuniformEXT(mat.albedoTex)], fragTexCoord);
    }
    if (baseColor.a < 0.1) discard;

    vec3 norm = normalize(fragNormal);
    vec3 lightDir = normalize(-ubo.cam.directionalLight.direction.xyz);
    vec3 LColor = ubo.cam.directionalLight.color.rgb * ubo.cam.directionalLight.intensity.x;
    if (length(LColor) < 0.01) LColor = vec3(1.0);

    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * baseColor.rgb * LColor;
    vec3 ambient = 0.05 * baseColor.rgb;
    
    vec3 viewDir = normalize(ubo.cam.cameraPos.xyz - fragPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(norm, halfwayDir), 0.0), 32.0);
    vec3 specular = vec3(mat.metallic * 0.2) * spec * LColor;
    
    vec3 result = ambient + diffuse + specular + mat.emission.rgb;
    result = result / (result + vec3(1.0));
    result = pow(result, vec3(1.0/2.2));
    outColor = vec4(result, 1.0);
}
