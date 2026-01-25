#pragma once

#include "pch.h"
#include <vulkan/vulkan.h>
#include <string_view>

namespace Chimera {
    namespace Config {
        // Window & Rendering Settings
        constexpr uint32_t WINDOW_WIDTH = 800;
        constexpr uint32_t WINDOW_HEIGHT = 600;
        constexpr int MAX_FRAMES_IN_FLIGHT = 3;

        // Asset Paths
        constexpr std::string_view MODEL_PATH = "assets/models/viking_room.obj";
        constexpr std::string_view TEXTURE_PATH = "assets/textures/viking_room.png";
        constexpr std::string_view SHADER_DIR = "shaders/";

        // Ray Tracing Settings
        constexpr int RT_MAX_RECURSION_DEPTH = 2;
        constexpr VkFormat RT_STORAGE_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;
    }
}
