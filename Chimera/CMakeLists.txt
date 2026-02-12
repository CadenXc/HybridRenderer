cmake_minimum_required(VERSION 3.20)
project(Chimera)

# 1. Add third_party
add_subdirectory(third_party)

# 2. Source Files
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

# 5. Precompiled Header
target_precompile_headers(Chimera PRIVATE src/pch.h)

# 6. Linking
target_link_libraries(Chimera PUBLIC
    glfw
    glm::glm
    volk
    VulkanMemoryAllocator
    stb_lib
    cgltf
    spirv-reflect
    spdlog::spdlog
    imgui
)

# 7. Modern Shader Compilation System
find_program(GLSLC_EXECUTABLE NAMES glslc HINTS ENV VULKAN_SDK PATH_SUFFIXES Bin)

if(GLSLC_EXECUTABLE)
    message(STATUS "Found glslc: ${GLSLC_EXECUTABLE}")
    
    set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    set(SHADER_BINARY_DIR "${CMAKE_BINARY_DIR}/shaders_compiled")
    file(MAKE_DIRECTORY ${SHADER_BINARY_DIR})

    file(GLOB_RECURSE GLSL_SOURCE_FILES 
        "${SHADER_SOURCE_DIR}/*.vert" 
        "${SHADER_SOURCE_DIR}/*.frag" 
        "${SHADER_SOURCE_DIR}/*.comp"
        "${SHADER_SOURCE_DIR}/*.rgen" 
        "${SHADER_SOURCE_DIR}/*.rchit" 
        "${SHADER_SOURCE_DIR}/*.rmiss"
    )

    set(SPIRV_BINARIES "")

    foreach(SHADER_FILE ${GLSL_SOURCE_FILES})
        # [FIX] 使用全路径相对于根目录生成输出名，防止冲突
        file(RELATIVE_PATH REL_PATH ${SHADER_SOURCE_DIR} ${SHADER_FILE})
        set(SPIRV_BINARY "${SHADER_BINARY_DIR}/${REL_PATH}.spv")
        
        # 确保输出目录存在
        get_filename_component(OUT_DIR ${SPIRV_BINARY} DIRECTORY)
        file(MAKE_DIRECTORY ${OUT_DIR})
        
        add_custom_command(
            OUTPUT ${SPIRV_BINARY}
            COMMAND ${GLSLC_EXECUTABLE} --target-env=vulkan1.3 -I "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Backend" -I "${SHADER_SOURCE_DIR}/common" -I "${SHADER_SOURCE_DIR}" ${SHADER_FILE} -o ${SPIRV_BINARY}
            DEPENDS ${SHADER_FILE}
            COMMENT "Compiling Chimera Shader: ${REL_PATH} to SPIR-V..."
        )
        list(APPEND SPIRV_BINARIES ${SPIRV_BINARY})
    endforeach()

    add_custom_target(ChimeraShaders ALL DEPENDS ${SPIRV_BINARIES})
else()
    message(FATAL_ERROR "glslc not found!")
endif()
