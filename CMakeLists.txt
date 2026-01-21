cmake_minimum_required(VERSION 3.20)

project(Chimera LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Dependencies
find_package(Vulkan REQUIRED)
add_subdirectory(third_party)

# 2. Source Files (Include src/Core, src/Renderer, etc.)
file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(${PROJECT_NAME} ${SOURCES})

# 3. Include Directories
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/src 
)

# 4. Linking
target_link_libraries(${PROJECT_NAME} PRIVATE
    glfw
    glm::glm
    volk
    VulkanMemoryAllocator
    tinyobjloader
    stb_lib
    spdlog::spdlog
)

# 5. Debugging Environment
set_target_properties(${PROJECT_NAME} PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
)

# 6. Shader Compilation (GLSL -> SPIR-V)
find_program(GLSLC_EXECUTABLE NAMES glslc HINTS ENV VULKAN_SDK PATH_SUFFIXES Bin)

if(GLSLC_EXECUTABLE)
    function(compile_shader SHADER_SOURCE)
        get_filename_component(SHADER_NAME ${SHADER_SOURCE} NAME)
        set(SPIRV_BINARY "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER_NAME}.spv")
        
        add_custom_command(
            OUTPUT ${SPIRV_BINARY}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
            COMMAND ${GLSLC_EXECUTABLE} ${SHADER_SOURCE} -o ${SPIRV_BINARY}
            DEPENDS ${SHADER_SOURCE}
            COMMENT "Compiling ${SHADER_NAME} to SPIR-V..."
        )
        target_sources(${PROJECT_NAME} PRIVATE ${SPIRV_BINARY})
    endfunction()

    file(GLOB_RECURSE GLSL_SOURCE_FILES 
        "shaders/*.vert" 
        "shaders/*.frag" 
        "shaders/*.rgen" 
        "shaders/*.rchit" 
        "shaders/*.rmiss"
    )

    foreach(SHADER_FILE ${GLSL_SOURCE_FILES})
        compile_shader(${SHADER_FILE})
    endforeach()
else()
    message(WARNING "Could not find glslc.exe! Shaders will not be compiled automatically.")
endif()

# 7. Post-Build: Copy Assets & Shaders
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets"

    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders"

    COMMENT "Updating Assets and Shaders..."
)