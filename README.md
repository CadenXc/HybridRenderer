# Chimera (HybridRenderer)

A real-time Hybrid Renderer engine built with Vulkan, featuring a robust Render Graph system and an interactive editor interface. This project serves as a graduation thesis, exploring the synergy between rasterization and ray tracing.

The architecture is heavily inspired by [Walnut](https://github.com/TheCherno/Walnut) for its application layer and [VulkanHybridRenderer](https://github.com/Ipotrick/VulkanHybridRenderer) for its hybrid rendering techniques.

![Chimera Banner](https://via.placeholder.com/1200x400.png?text=Chimera+Hybrid+Renderer+Preview)
*(Screenshot placeholder)*

## Features

### Rendering
*   **Hybrid Rendering Pipeline**: Seamlessly combines traditional rasterization with hardware-accelerated ray tracing (RTX).
*   **Render Graph**: A flexible, graph-based rendering architecture for managing complex pass dependencies and resource transitions.
*   **Ray Tracing Effects**:
    *   Ray Traced Shadows
    *   Ray Traced Reflections (Work in Progress)
    *   Ambient Occlusion
*   **Forward Rendering**: Standard forward rendering path for non-RT hardware or fallback.

### Core Architecture
*   **Layer System**: Modular application structure allowing easily stackable logic (e.g., Editor, Game, Overlay).
*   **Vulkan Backend**: Low-level, high-performance rendering backend using modern Vulkan features (via `Volk` meta-loader).
*   **ImGui Editor**: Fully integrated debugging and editing tools using Dear ImGui.
*   **Scene Management**: Support for loading 3D models (OBJ/GLTF).

## Getting Started

### Requirements
*   **OS**: Windows 10/11 (64-bit)
*   **GPU**: Vulkan 1.2+ compatible GPU (RTX series recommended for Ray Tracing features)
*   **Build Tools**:
    *   [CMake](https://cmake.org/) (3.20+)
    *   [Visual Studio 2022](https://visualstudio.com/) (recommended)
*   **SDKs**:
    *   [Vulkan SDK](https://vulkan.lunarg.com/sdk/home#windows)

### Installation & Build

1.  **Clone the repository and its dependencies**:
    ```bash
    git clone --recursive https://github.com/Xiang-G/HybridRenderer.git
    cd HybridRenderer
    ```

    If the repository was cloned without `--recursive`, initialize it from the repository root:

    ```bash
    git submodule sync --recursive
    git submodule update --init --recursive --checkout
    ```

    Submodules are pinned to exact commits by the parent repository. A detached `HEAD` inside a submodule is expected and preserves the dependency versions used by this project.

2.  **Generate Project Files**:
    ```bash
    cmake -S . -B build
    ```

3.  **Build**:
    ```bash
    cmake --build build --config Debug
    ```
    *Alternatively, open `build/HybridRenderer.sln` in Visual Studio and build the `Sandbox` project.*

4.  **Run**:
    The executable will be located in `build/Sandbox/Debug/Sandbox.exe`.

### Submodule Troubleshooting

Check the current submodule state from the repository root:

```bash
git submodule status --recursive
```

*   A leading `-` means the submodule has not been initialized.
*   A leading `+` means the checked-out commit differs from the commit recorded by the parent repository.
*   No prefix means the submodule is at the recorded commit.

For normal recovery, run:

```bash
git submodule sync --recursive
git submodule update --init --recursive --checkout
```

If disposable third-party working-tree changes prevent checkout, the recorded versions can be restored forcibly:

```bash
git submodule update --init --recursive --checkout --force
```

> **Warning:** `--force` discards local changes inside submodules. Back up any intentional dependency modifications first.

Do not use `git submodule update --remote` or run `git pull` inside submodules when reproducing the recorded project state; those commands can move dependencies to newer upstream commits.

Errors such as `not our ref` or `did not contain <SHA>` mean the configured upstream no longer provides the recorded commit. Recover that commit from a fork, archive, or older clone that still contains it.

## Controls

*   **Right-Click + Drag**: Rotate Camera
*   **WASD**: Move Camera
*   **Q/E**: Move Up/Down
*   **Scroll**: Adjust Movement Speed

## Architecture

The project follows a clean separation of concerns:

*   **Chimera**: The core engine library.
    *   `src/core`: Application lifecycle, Layers, Logging.
    *   `src/rendering_backend`: Low-level Vulkan abstractions (Context, Buffers, Images, Pipelines).
    *   `src/render_graph`: Dependency management for rendering passes.
    *   `src/render_paths`: Concrete implementations of rendering logic (Forward, Hybrid, RayTracing).
*   **Sandbox**: The client application that utilizes the Chimera engine.

## Libraries and Dependencies

*   [Vulkan](https://www.khronos.org/vulkan/) - Graphics API
*   [Volk](https://github.com/zeux/volk) - Meta-loader for Vulkan
*   [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) - GPU memory management
*   [Dear ImGui](https://github.com/ocornut/imgui) - Bloat-free Graphical User interface
*   [GLFW](https://github.com/glfw/glfw) - Window and input management
*   [GLM](https://github.com/g-truc/glm) - Mathematics library
*   [spdlog](https://github.com/gabime/spdlog) - Fast C++ logging library
*   [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader) - OBJ model loading
*   [cgltf](https://github.com/jkuhlmann/cgltf) - GLTF model loading
*   [stb_image](https://github.com/nothings/stb) - Image loading

## Credits

*   **Walnut**: Application framework structure reference.
*   **VulkanHybridRenderer**: Hybrid rendering technique reference.

## License

This project is licensed under the [MIT License](LICENSE).
