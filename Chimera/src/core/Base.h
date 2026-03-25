#pragma once

#include <memory>
#include <stdexcept>
#include "Core/Log.h"

// --- Vulkan Error Checking ---
#define VK_CHECK(result) \
    do { \
        VkResult res = (result); \
        if (res != VK_SUCCESS) { \
            CH_CORE_ERROR("Vulkan Error: {} in {} at line {}", (int)res, __FILE__, __LINE__); \
            throw std::runtime_error("Vulkan error"); \
        } \
    } while(0)

namespace Chimera
{
    // --- Smart Pointer Aliases ---
    template<typename T>
    using Scope = std::unique_ptr<T>;
    template<typename T, typename ... Args>
    constexpr Scope<T> CreateScope(Args&& ... args) {
        return std::make_unique<T>(std::forward<Args>(args)...);
    }

    template<typename T>
    using Ref = std::shared_ptr<T>;
    template<typename T, typename ... Args>
    constexpr Ref<T> CreateRef(Args&& ... args) {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }
}
