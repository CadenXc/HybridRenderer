cmake_minimum_required(VERSION 3.20)

project(HybridRenderer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==============================================================================
# 1. 查找系统 Vulkan SDK
# ==============================================================================
find_package(Vulkan REQUIRED)

# ==============================================================================
# 2. 添加第三方库 (管理所有依赖)
# ==============================================================================
add_subdirectory(third_party)

# ==============================================================================
# 3. 定义主程序
# ==============================================================================
file(GLOB_RECURSE SOURCES "src/*.cpp")
add_executable(${PROJECT_NAME} ${SOURCES})

# ==============================================================================
# 4. 链接库
# ==============================================================================
target_link_libraries(${PROJECT_NAME} PRIVATE
    glfw
    glm::glm
    volk
    VulkanMemoryAllocator
    tinyobjloader
    stb_lib
)

# ==============================================================================
# 5. 配置 VS 调试环境
# ==============================================================================
# 这一步确保 VS 按 F5 调试时，工作目录是 .exe 所在的目录
set_target_properties(${PROJECT_NAME} PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
)

# ==============================================================================
# 6. 自动编译 Shaders (GLSL -> SPIR-V)
# ==============================================================================
find_program(GLSLC_EXECUTABLE NAMES glslc HINTS ENV VULKAN_SDK PATH_SUFFIXES Bin)

if(GLSLC_EXECUTABLE)
    # 定义编译函数
    function(compile_shader SHADER_SOURCE)
        get_filename_component(SHADER_NAME ${SHADER_SOURCE} NAME)
        # 编译结果放到构建目录下的 shaders 文件夹里
        set(SPIRV_BINARY "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER_NAME}.spv")
        
        add_custom_command(
            OUTPUT ${SPIRV_BINARY}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
            COMMAND ${GLSLC_EXECUTABLE} ${SHADER_SOURCE} -o ${SPIRV_BINARY}
            DEPENDS ${SHADER_SOURCE}
            COMMENT "Compiling ${SHADER_NAME} to SPIR-V..."
        )
        # 将生成的 .spv 加入目标，确保 VS 构建系统能感知到它
        target_sources(${PROJECT_NAME} PRIVATE ${SPIRV_BINARY})
    endfunction()

    # 扫描目录下所有 Shader 源文件
    file(GLOB_RECURSE GLSL_SOURCE_FILES 
        "shaders/*.vert" 
        "shaders/*.frag" 
        "shaders/*.rgen" 
        "shaders/*.rchit" 
        "shaders/*.rmiss"
    )

    # 逐个编译
    foreach(SHADER_FILE ${GLSL_SOURCE_FILES})
        compile_shader(${SHADER_FILE})
    endforeach()
else()
    message(WARNING "Could not find glslc.exe! Shaders will not be compiled automatically.")
endif()

# ==============================================================================
# 7. [关键] 编译后自动复制资源
# ==============================================================================
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    # 1. 复制 Assets (模型、纹理) 到 exe 目录
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets"

    # 2. [关键修复] 复制 刚刚编译好的 .spv 文件 到 exe 目录
    # 注意：源路径是 CMAKE_CURRENT_BINARY_DIR (构建生成目录)，不是 SOURCE_DIR
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders"

    COMMENT "Updating Assets and Shaders in executable directory..."
)