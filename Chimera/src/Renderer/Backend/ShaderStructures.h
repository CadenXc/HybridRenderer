#pragma once

#include <glm/glm.hpp>
#include "Renderer/Resources/ResourceHandle.h"

namespace Chimera {

    // This file defines structures shared between C++ and GLSL.
    // We use alignas(16) to match std140/std430 layout requirements in shaders.

    struct PBRMaterial {
        alignas(16) glm::vec4 albedo;
        alignas(16) glm::vec3 emission;
        float roughness;
        float metallic;
        
        TextureHandle albedoTex;
        TextureHandle normalTex;
        TextureHandle metalRoughTex;
        int padding; // Maintain 16-byte alignment for the whole struct if needed
    };

    struct RTInstanceData {
        uint64_t vertexAddress;
        uint64_t indexAddress;
        int materialIndex;
        int padding;
    };

    // --- Push Constants ---

    struct GBufferPushConstants {
        glm::mat4 model;
        int materialIndex;
    };

    struct ForwardPushConstants {
        glm::mat4 model;
        glm::mat4 normalMatrix;
        int materialIndex;
    };

    struct RaytracePushConstants {
        glm::vec4 clearColor;
        glm::vec3 lightPos;
        float lightIntensity;
        int frameCount;
        int skyboxIndex;
    };

}
