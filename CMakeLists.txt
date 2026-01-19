cmake_minimum_required(VERSION 3.20)

project(HybridRenderer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==============================================================================
# 1. 依赖库配置 (Vulkan, GLFW, GLM)
# ==============================================================================

# A. 查找 Vulkan SDK
find_package(Vulkan REQUIRED)

# B. 自动下载并配置 GLFW
include(FetchContent)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.3.8
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# C. 自动下载并配置 GLM (使用 1.0.1 修复兼容性)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
)
set(GLM_TEST_ENABLE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm)

# ==============================================================================
# 2. 定义可执行文件
# ==============================================================================

# 递归查找 src 目录下所有源文件
file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(${PROJECT_NAME} ${SOURCES})

# ==============================================================================
# 3. 链接与包含
# ==============================================================================

target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
)

# 4.  Visual Studio 调试工作目录
# 这行代码告诉 VS："当你按 F5 时，请把工作目录设为 .exe 所在的文件夹"
# 这样程序才能找到旁边的 shaders 和 assets 文件夹
set_target_properties(${PROJECT_NAME} PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
)

# ==============================================================================
# 5. 编译后自动复制资源
# ==============================================================================
# POST_BUILD 确保每次编译成功后，都会执行复制操作
# 将根目录下的 shaders 和 assets 复制到 .exe 生成的目录 (build/Debug/...)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets"
    COMMENT "正在将 Shaders 和 Assets 复制到构建目录..."
)