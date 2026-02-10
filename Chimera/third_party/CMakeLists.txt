if(NOT TARGET glfw)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    
    add_subdirectory(glfw)
endif()

if(NOT TARGET glm)
    set(GLM_TEST_ENABLE OFF CACHE BOOL "" FORCE)
    add_subdirectory(glm)
endif()

if(NOT TARGET volk)
    add_subdirectory(volk)
endif()

if(NOT TARGET VulkanMemoryAllocator)
    add_subdirectory(VulkanMemoryAllocator)
endif()

if(NOT TARGET stb_lib)
    add_library(stb_lib INTERFACE)
    target_include_directories(stb_lib INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/stb")
endif()

if(NOT TARGET spdlog)
    set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)

    add_subdirectory(spdlog)
endif()

if(NOT TARGET cgltf)
    add_library(cgltf INTERFACE)
    
    target_include_directories(cgltf INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/cgltf)
endif()

if(NOT TARGET spirv-reflect)
    set(SPIRV_REFLECT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/spirv-reflect")
    add_library(spirv-reflect STATIC 
        "${SPIRV_REFLECT_DIR}/spirv_reflect.c"
        "${SPIRV_REFLECT_DIR}/spirv_reflect.h"
    )
    target_include_directories(spirv-reflect PUBLIC "${SPIRV_REFLECT_DIR}")
endif()

if(NOT TARGET imgui)
    set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/imgui")
    
    file(GLOB IMGUI_SOURCES
        "${IMGUI_DIR}/*.cpp"
        "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
        "${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp"
    )

    add_library(imgui STATIC ${IMGUI_SOURCES})

    target_include_directories(imgui PUBLIC 
        "${IMGUI_DIR}" 
        "${IMGUI_DIR}/backends"
    )

    target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK)

    # ImGui 需要链接 Vulkan 和 GLFW
    target_link_libraries(imgui PUBLIC Vulkan::Vulkan glfw volk)
endif()