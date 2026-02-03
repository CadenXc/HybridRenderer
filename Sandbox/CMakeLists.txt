cmake_minimum_required(VERSION 3.20)
project(Sandbox)

# Source Files
file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(Sandbox ${SOURCES})

# Link Engine
target_link_libraries(Sandbox PRIVATE Chimera)

if(TARGET ChimeraShaders)
    # Create a target that ALWAYS runs (ALL) to copy shaders
    add_custom_target(UpdateShaders ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_BINARY_DIR}/shaders_compiled"
            "$<TARGET_FILE_DIR:Sandbox>/shaders"
        COMMENT "Updating Shaders in Sandbox Bin Directory..."
        DEPENDS ChimeraShaders
    )
    add_dependencies(Sandbox UpdateShaders)
endif()

# Post-Build: Copy Assets (Keep assets as POST_BUILD as they change less often, or move them too if desired. Keeping for now.)
add_custom_command(TARGET Sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:Sandbox>/assets"
    COMMENT "Updating Assets..."
)