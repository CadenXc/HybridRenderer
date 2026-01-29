cmake_minimum_required(VERSION 3.20)
project(Sandbox)

# Source Files
file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(Sandbox ${SOURCES})

# Link Engine
target_link_libraries(Sandbox PRIVATE Chimera)

if(TARGET ChimeraShaders)
    add_dependencies(Sandbox ChimeraShaders)
endif()

# Debugging Environment
set_target_properties(Sandbox PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:Sandbox>"
)

# Post-Build: Copy Assets & Shaders
add_custom_command(TARGET Sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:Sandbox>/assets"

    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_BINARY_DIR}/shaders_compiled"
        "$<TARGET_FILE_DIR:Sandbox>/shaders"

    COMMENT "Updating Assets and Shaders..."
)