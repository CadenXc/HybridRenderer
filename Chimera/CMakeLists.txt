cmake_minimum_required(VERSION 3.20)
project(Chimera)

# 1. Add third_party (moved here)
add_subdirectory(third_party)

# 2. Source Files (Phase 2: Common Utilities added)
file(GLOB_RECURSE SOURCES "src/*.cpp")

add_library(Chimera STATIC ${SOURCES})

# 3. Include Directories
target_include_directories(Chimera PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/src 
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

# 4. Global Definitions
target_compile_definitions(Chimera PUBLIC
    VK_NO_PROTOTYPES
    VK_USE_PLATFORM_WIN32_KHR
    GLFW_INCLUDE_VULKAN
    GLFW_EXPOSE_NATIVE_WIN32
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_ENABLE_EXPERIMENTAL
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

# 5. Precompiled Header (PCH)
target_precompile_headers(Chimera PRIVATE src/pch.h)

# 6. Linking
target_link_libraries(Chimera PUBLIC
    glfw
    glm::glm
    volk
    VulkanMemoryAllocator
    tinyobjloader
    stb_lib
    cgltf
    spdlog::spdlog
    imgui
)