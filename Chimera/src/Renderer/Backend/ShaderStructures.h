#pragma once

#include <glm/glm.hpp>
#include "Renderer/Resources/ResourceHandle.h"

namespace Chimera {

    // This file defines structures shared between C++ and GLSL.
    // We use alignas(16) to match std140/std430 layout requirements in shaders.

    struct PBRMaterial {
        glm::vec4 albedo;
        glm::vec4 emission; // Use vec4 for 16-byte alignment
        float roughness;
        float metallic;
        int albedoTex;
        int normalTex;
        int metalRoughTex;
        int padding[3]; // Explicit padding to make the struct 64 bytes (multiple of 16)
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
