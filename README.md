# Chimera Hybrid Renderer

Chimera is a Windows-only, real-time hybrid rendering project built with
C++20 and Vulkan 1.3. It began as a graduation project and is now being
developed as a learning-oriented renderer for studying rasterization,
hardware ray tracing, render graphs, temporal reconstruction, denoising, and
Vulkan correctness.

> [!IMPORTANT]
> Chimera is an experimental renderer, not a production-ready engine. The
> project has broad feature coverage, but several rendering contracts and
> fallback paths still need dedicated correctness tests. The current priority
> is reproducible, validation-clean rendering before adding more effects.

The repository includes a small distributable glTF scene, so a recursive
clone can build and launch without downloading a large external model.

## Project Status

| Area | Status | Notes |
| --- | --- | --- |
| Forward path | Implemented | Forward shading, TAA, and post-processing are connected. This is the intended baseline path for correctness work. |
| Hybrid path | Experimental | G-buffer rasterization feeds ray-traced shadows/AO, reflections, diffuse GI, optional SVGF, composition, and post-processing. |
| Ray-traced path | Experimental | Depth prepass, ray-traced scene rendering, TAA, and post-processing are connected. Requires the complete RT capability set. |
| Render Graph | Working prototype | Supports declared resources, history resources, pass execution, barriers, GPU timestamps, and Mermaid export. It is not yet a full dependency compiler with complete RAW/WAR/WAW analysis, topological sorting, and invariant checking. |
| Scene and assets | Implemented with limitations | Asynchronous model import, glTF/OBJ loading, materials, bindless textures, scene instances, and BLAS/TLAS construction are present. |
| Editor and diagnostics | Implemented | Runtime path switching, effect toggles, debug views, scene controls, frame statistics, per-pass GPU timing, and capability logging. |
| Automated tests and CI | Not yet available | The project currently has no first-party unit-test suite or CI pipeline. |
| Non-RT fallback | Not fully validated | Device creation distinguishes base and ray-tracing capabilities, but the complete experience on non-RT hardware is still under development. |

Recent correctness work has centralized per-frame rendering, fixed swapchain
acquire synchronization for the initial transfer clear, propagated resize
events through the active render path, hardened empty Render Graph execution,
improved shutdown ordering, and added explicit device capability checks. These
changes establish a better baseline, but they do not imply that every path and
feature combination has been exhaustively validated.

For the detailed audit, current risks, and learning roadmap, see the
[Chinese deep-analysis report](docs/analysis/HybridRenderer-Deep-Analysis-ZH.md).

## Rendering Architecture

```text
Sandbox / ImGui Editor
        |
        v
Application + Scene + ResourceManager
        |
        v
Forward / Hybrid / Ray-Traced RenderPath
        |
        v
Render Graph -> Graphics / Compute / Ray-Tracing Passes
        |
        v
Vulkan Backend -> Command Buffers -> Swapchain / GPU

GLSL -> glslc -> SPIR-V -> Runtime Reflection -> Pipelines
```

The Hybrid path currently follows this high-level data flow:

```text
G-Buffer
   +-> RT shadows + AO --+
   +-> RT reflections ---+-> optional SVGF -> composition -> post-process
   +-> RT diffuse GI ----+
```

Key source locations:

- `Chimera/src/core` — application lifecycle, windowing, input, layers, and tasks
- `Chimera/src/Renderer/Backend` — Vulkan instance/device/swapchain, resources, descriptors, shaders, and pipelines
- `Chimera/src/Renderer/Graph` — Render Graph resources, compilation, execution contexts, and barriers
- `Chimera/src/Renderer/Pipelines` — Forward, Hybrid, and Ray-Traced paths
- `Chimera/src/Renderer/Passes` — graphics, compute, post-process, and ray-tracing passes
- `Chimera/src/Renderer/Resources` — renderer-facing resource management
- `Chimera/src/Scene` — scenes, models, cameras, lights, and acceleration structures
- `Chimera/shaders` — GLSL graphics, compute, and ray-tracing shaders
- `Sandbox/src/editor` — ImGui editor and runtime controls

## Requirements

- Windows 10 or Windows 11, 64-bit
- CMake 3.24 or newer
- Visual Studio 2022 or Visual Studio 2026 with the C++ desktop workload
- A Vulkan SDK that provides `Vulkan::Vulkan` and `Vulkan::glslc`
- A Vulkan 1.3-capable GPU and driver

The base renderer currently checks these features at startup:

- sampler anisotropy and 64-bit shader integers
- buffer device address
- descriptor indexing and the bindless descriptor features used by Chimera
- scalar block layout and host query reset
- dynamic rendering, synchronization2, and shader demote-to-helper invocation

The Hybrid and Ray-Traced paths additionally require the ray-tracing pipeline,
acceleration structure, ray query, deferred host operations, and related
descriptor update-after-bind support. The application prints a capability
matrix during startup and stops when required base features are missing.

## Build

Clone the repository together with its pinned dependencies:

```powershell
git clone --recursive https://github.com/CadenXc/HybridRenderer.git
cd HybridRenderer
```

If the repository was cloned without `--recursive`:

```powershell
git submodule sync --recursive
git submodule update --init --recursive --checkout
```

### Visual Studio 2022

Configure the build tree, then compile the Debug configuration:

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-debug
```

For Release:

```powershell
cmake --build --preset vs2022-release
```

The generated solution is `build/vs2022/HybridRenderer.sln`. The Debug
executable is normally written to:

```text
build/vs2022/Sandbox/Debug/Sandbox.exe
```

### Visual Studio 2026

Equivalent presets are available for Visual Studio 2026:

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-debug
```

Use `vs2026-release` for the Release configuration.

The configure command generates the selected build tree and Visual Studio
solution. The build command compiles one configuration from that tree; they
are two distinct steps.

## Run and Explore

`Sandbox` starts with the embedded Box glTF smoke-test asset and selects the
Hybrid path. The editor can switch between Forward, Hybrid, and Ray Tracing at
runtime.

Camera controls:

| Input | Action |
| --- | --- |
| `W`, `A`, `S`, `D` | Move the camera focal point |
| `Q`, `E` | Move down or up |
| `Left Shift` | Increase movement speed |
| `Alt` + left mouse | Rotate |
| `Alt` + middle mouse | Pan |
| `Alt` + right mouse | Zoom |
| Mouse wheel | Zoom |

The control panel exposes:

- render-path selection and intermediate-buffer debug views
- ray-traced shadows, AO, reflections, and diffuse GI toggles
- SVGF temporal/spatial filtering and TAA controls
- light, exposure, ambient, IBL, and emissive settings
- model/HDR discovery, scene hierarchy, and transform editing
- frame statistics, per-pass GPU timestamps, and Render Graph Mermaid export

Feature toggles are useful for investigation, but not every combination is a
validated rendering configuration yet.

## Assets

The default asset is the Khronos glTF Sample Assets `Box`, created by Cesium
and distributed under CC BY 4.0. Its attribution and license are preserved in
`Sandbox/assets/models/smoke_test`.

Larger scenes such as Sponza are intentionally not bundled. Additional models
can be placed under `Sandbox/assets/models`, and HDR environments under
`Sandbox/assets/textures/hdr`, for discovery by the editor. Verify the license
of every asset before redistributing it with the project.

## Dependencies

Dependencies are pinned as Git submodules:

- [Vulkan](https://www.vulkan.org/) and [Volk](https://github.com/zeux/volk)
- [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
- [GLFW](https://github.com/glfw/glfw), [GLM](https://github.com/g-truc/glm), and [Dear ImGui](https://github.com/ocornut/imgui)
- [Assimp](https://github.com/assimp/assimp), [cgltf](https://github.com/jkuhlmann/cgltf), and [stb](https://github.com/nothings/stb)
- [SPIRV-Reflect](https://github.com/KhronosGroup/SPIRV-Reflect)
- [spdlog](https://github.com/gabime/spdlog)

Each dependency remains subject to its own license.

## Known Limitations and Roadmap

Near-term priorities are:

1. Add first-party tests for TaskSystem behavior, Render Graph invariants, and CPU/GPU shader ABI layouts.
2. Complete Render Graph RAW/WAR/WAW dependency modeling, topological sorting, cycle detection, and actionable compile errors.
3. Define history-resource behavior across first use, resize, camera cuts, scene changes, and render-path switches.
4. Validate all TAA/SVGF flag combinations and non-RT fallback behavior.
5. Audit SBT alignment and visibility, acceleration-structure replacement lifetimes, and descriptor contracts.
6. Add deterministic captures, image comparisons, benchmark scenes, and CI for non-GPU checks and shader compilation.

The long-term goal is an observable and verifiable Vulkan hybrid renderer:
one where resource lifetimes, barriers, history, capabilities, and GPU timings
can be inspected rather than inferred from a final image alone.

## Submodule Troubleshooting

Inspect the pinned dependency state with:

```powershell
git submodule status --recursive
```

- A leading `-` means the submodule is not initialized.
- A leading `+` means its checkout differs from the commit recorded by this repository.
- No prefix means it is at the recorded commit.

Restore the recorded revisions with:

```powershell
git submodule sync --recursive
git submodule update --init --recursive --checkout
```

Avoid `git submodule update --remote` when reproducing the project: it can move
dependencies beyond the commits tested by this repository. Using `--force`
can discard local dependency changes, so back them up before choosing it.

## Acknowledgements

The application/layer structure was influenced by
[Walnut](https://github.com/TheCherno/Walnut), and the exploration of hybrid
rendering was influenced by
[VulkanHybridRenderer](https://github.com/Ipotrick/VulkanHybridRenderer).
These are architectural and learning references; Chimera is an independent
project.

## License

Chimera is available under the [MIT License](LICENSE).
Bundled assets and third-party dependencies retain their own licenses.
