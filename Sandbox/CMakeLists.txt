cmake_minimum_required(VERSION 3.20)
project(Sandbox)

# Source Files
file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(Sandbox ${SOURCES})

# Link Engine
target_link_libraries(Sandbox PRIVATE Chimera)

# Debugging Environment
set_target_properties(Sandbox PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:Sandbox>"
)

# Shader Compilation
find_program(GLSLC_EXECUTABLE NAMES glslc HINTS ENV VULKAN_SDK PATH_SUFFIXES Bin)

if(GLSLC_EXECUTABLE)
    function(compile_shader SHADER_SOURCE)
        get_filename_component(SHADER_NAME ${SHADER_SOURCE} NAME)
        set(SPIRV_BINARY "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER_NAME}.spv")
        
        add_custom_command(
            OUTPUT ${SPIRV_BINARY}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
            COMMAND ${GLSLC_EXECUTABLE} --target-env=vulkan1.3 ${SHADER_SOURCE} -o ${SPIRV_BINARY}
            DEPENDS ${SHADER_SOURCE}
            COMMENT "Compiling ${SHADER_NAME} to SPIR-V..."
        )
        target_sources(Sandbox PRIVATE ${SPIRV_BINARY})
    endfunction()

    file(GLOB_RECURSE GLSL_SOURCE_FILES 
        "shaders/*.vert" 
        "shaders/*.frag" 
        "shaders/*.comp"
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

# Post-Build: Copy Assets & Shaders
add_custom_command(TARGET Sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:Sandbox>/assets"

    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        "$<TARGET_FILE_DIR:Sandbox>/shaders"

    COMMENT "Updating Assets and Shaders..."
)
