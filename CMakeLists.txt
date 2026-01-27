cmake_minimum_required(VERSION 3.20)

project(HybridRenderer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dependencies (Vulkan is still system-wide usually)
find_package(Vulkan REQUIRED)

# Modules
add_subdirectory(Chimera)
add_subdirectory(Sandbox)

# Set Sandbox as the startup project for Visual Studio
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Sandbox)