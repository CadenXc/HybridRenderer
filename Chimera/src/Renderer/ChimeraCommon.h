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

namespace Chimera
{
static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;

    // --- Render Target Formats ---
static constexpr VkFormat HDR_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
static constexpr VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;

    // --- [FINALIZED] Reversed-Z Configuration ---
    // Near=1.0, Far=0.0. Clear to 0.0, use GreaterOrEqual test.
static constexpr float CH_DEPTH_CLEAR_VALUE = 0.0f;
static constexpr VkCompareOp CH_DEPTH_COMPARE_OP =
    VK_COMPARE_OP_GREATER_OR_EQUAL;

    // --- App Structures ---
struct ApplicationSpecification
{
    std::string Name = "Chimera App";
    uint32_t Width = 1600;
    uint32_t Height = 900;
    bool Fullscreen = false;
    bool VSync = true;

        // Paths
    std::string AssetDir = "assets/";
    std::string ShaderDir = "shaders/";
    std::string ShaderSourceDir = ""; // Will be auto-detected if empty

        // Initial Settings
    glm::vec4 ClearColor = {0.1f, 0.1f, 0.1f, 1.0f};
    uint32_t DisplayMode = 0;
    uint32_t RenderFlags = 1;
    bool EnableRayTracing = true;
};

    // --- PURE FORWARD DECLARATIONS ONLY ---
class VulkanContext;
class Scene;
class RenderGraph; // MUST BE PURE FORWARD DECLARATION
struct RenderFrameInfo;

struct RenderFrameInfo
{
    VkCommandBuffer commandBuffer;
    uint32_t frameIndex;
    uint32_t imageIndex;
    VkDescriptorSet globalSet;
};

enum class RenderPathType
{
    Forward = 0,
    Hybrid,
    RayTracing
};

inline const char* RenderPathTypeToString(RenderPathType type)
{
    switch (type)
    {
        case RenderPathType::Forward:
            return "Forward";
        case RenderPathType::Hybrid:
            return "Hybrid";
        case RenderPathType::RayTracing:
            return "RayTracing";
        default:
            return "Unknown";
    }
}

inline std::vector<RenderPathType> GetAllRenderPathTypes()
{
    return {RenderPathType::Forward, RenderPathType::Hybrid,
            RenderPathType::RayTracing};
}
} // namespace Chimera
