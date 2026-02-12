#pragma once

#include "volk.h"
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>
#include <memory>
#include <array>
#include <stdexcept>
#include <algorithm>
#include <functional>

#include "Core/Log.h"

namespace Chimera
{
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;

    struct ApplicationSpecification {
        std::string Name = "Chimera App";
        uint32_t Width = 1280;
        uint32_t Height = 720;
        bool Fullscreen = false;
        bool VSync = true;
    };

    // --- PURE FORWARD DECLARATIONS ONLY ---
    class VulkanContext;
    class Scene;
    class RenderGraph; // MUST BE PURE FORWARD DECLARATION
    struct RenderFrameInfo;

    struct RenderFrameInfo {
        VkCommandBuffer commandBuffer;
        uint32_t frameIndex;
        uint32_t imageIndex;
        VkDescriptorSet globalSet;
    };

    enum class RenderPathType { Forward = 0, Hybrid, RayTracing };

    inline const char* RenderPathTypeToString(RenderPathType type) {
        switch (type) {
            case RenderPathType::Forward: return "Forward";
            case RenderPathType::Hybrid: return "Hybrid";
            case RenderPathType::RayTracing: return "RayTracing";
            default: return "Unknown";
        }
    }

    inline std::vector<RenderPathType> GetAllRenderPathTypes() { return { RenderPathType::Forward, RenderPathType::Hybrid, RenderPathType::RayTracing }; }
}