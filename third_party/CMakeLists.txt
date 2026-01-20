# third_party/CMakeLists.txt

# ==========================================================
# 1. GLFW
# ==========================================================
if(NOT TARGET glfw)
    # 设置 GLFW 选项，防止它编译文档和测试，加快构建速度
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    
    add_subdirectory(glfw)
endif()

# ==========================================================
# 2. GLM
# ==========================================================
if(NOT TARGET glm)
    set(GLM_TEST_ENABLE OFF CACHE BOOL "" FORCE)
    add_subdirectory(glm)
endif()

# ==========================================================
# 3. Volk (Vulkan Meta Loader)
# ==========================================================
if(NOT TARGET volk)
    # Volk 是一个静态库，add_subdirectory 会自动处理
    add_subdirectory(volk)
endif()

# ==========================================================
# 4. VMA (Vulkan Memory Allocator)
# ==========================================================
if(NOT TARGET VulkanMemoryAllocator)
    # VMA 也是个 Interface 或 Library，直接添加
    add_subdirectory(VulkanMemoryAllocator)
endif()

# ==========================================================
# 5. TinyObjLoader
# ==========================================================
if(NOT TARGET tinyobjloader)
    set(TINYOBJLOADER_BUILD_TEST_LOADER OFF CACHE BOOL "" FORCE)
    set(TINYOBJLOADER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(tinyobjloader)
endif()

# ==========================================================
# 6. STB (Header-only library)
# ==========================================================
# STB 没有 CMakeLists.txt，我们手动定义它
if(NOT TARGET stb_lib)
    add_library(stb_lib INTERFACE)
    target_include_directories(stb_lib INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/stb")
endif()