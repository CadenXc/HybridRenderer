# Changelog

Notable changes to HybridRenderer are recorded here. The project follows
semantic versioning once a release is tagged.

## [0.1.0] - 2026-08-18

This is the first public release of the renderer.

### Added

- Forward, Hybrid, and Ray Traced render paths with runtime switching.
- Render Graph resource declarations, history resources, whole-resource
  RAW/WAR/WAW dependency compilation, topological execution layers, cycle
  rejection, barriers, GPU timestamps, and Mermaid export.
- TAA, ray-traced shadows/AO/reflections/diffuse GI, SVGF denoising, frame
  capture, image regression tools, and benchmark CSV export.
- Vulkan capability reporting and explicit base/ray-tracing requirements.
- Eight CTest executables covering core scheduling, Render Graph contracts,
  shader ABI, image comparison, resource identity, asset import, camera math,
  and benchmark recording.
- Visual Studio 2022/2026 CMake presets and Windows Release CI.

### Changed

- Centralized per-frame rendering ownership in `Application`.
- Made shader descriptor resolution name-based and validated its contracts.
- Made history-resource fallbacks explicit for temporal passes.
- Made model GPU upload a one-shot operation and preserved mesh-to-BLAS slot
  identity across failed or empty geometries.

### Fixed

- Swapchain acquire synchronization for transfer clears and image state
  transitions.
- Resize propagation, empty Render Graph execution, shutdown ordering, and
  nested task waits.
- Same-state write-after-write barriers and missing RAW/WAR/WAW dependencies.
- Ray-tracing SBT visibility/alignment, acceleration-structure scratch
  alignment, per-mesh BLAS bounds, replacement lifetime, and null BLAS use.
- Scene reloads now preserve the global material and instance buffers referenced
  by persistent descriptor sets.

### Known limitations

- The renderer is Windows-only and remains experimental.
- CI validates compilation, shader compilation, and CPU-side tests, but does
  not run the interactive Vulkan renderer on a GPU.
- Non-RT hardware fallback and all temporal-effect combinations are not yet
  exhaustively validated.
- The Hybrid path keeps its ray-traced shadow, reflection, and diffuse-GI
  passes in the Render Graph whenever ray tracing and a TLAS are available.
  Feature flags suppress shader contributions but do not remove those GPU
  dispatches, so disabled effects can still have measurable pass cost.
- Render Graph tracking is whole-resource based and does not schedule across
  multiple Vulkan queues.
