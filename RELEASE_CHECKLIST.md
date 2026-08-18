# HybridRenderer v0.1 Release Checklist

This checklist separates reproducible evidence from assumptions. A green CPU
test suite does not prove that a Vulkan frame is visually correct.

## Completed locally (2026-08-18)

- [x] Visual Studio 2026 Debug builds successfully.
- [x] Visual Studio 2026 Release builds successfully.
- [x] All eight CTest targets pass in Debug and Release.
- [x] Shaders compile as part of both builds.
- [x] Forward, Hybrid, and Ray Traced paths launch and render the embedded Box
      smoke scene on an NVIDIA GeForce RTX 5070 Ti.
- [x] Synchronization validation is enabled for the local smoke run with no
      reported synchronization error.
- [x] Graceful shutdown completes with no VMA leak reported.

## Required before tagging v0.1.0

- [ ] Push the branch and confirm `Windows CI` passes on GitHub.
- [x] Repeat the documented VS2022 Release configure/build/test commands from
      a clean recursive checkout. GitHub Actions run `32090847097` completed
      the VS2022 Release build and all eight tests.
- [ ] Review the README, changelog, license, and known limitations as they
      appear on GitHub.
- [x] Capture one deterministic smoke baseline for each render path and record
      GPU, driver, resolution, scene, and feature flags.
- [ ] Move the changelog entries from `Unreleased` to `0.1.0` with the release
      date.
- [ ] Create the `v0.1.0` tag and GitHub release; attach screenshots and the
      benchmark CSV rather than binaries with unverified runtime dependencies.

## Evidence boundaries

- CTest covers CPU-side invariants and shader-facing data contracts. It does
  not submit representative frames to a GPU in CI.
- A validation-clean smoke run covers only the exercised GPU, driver, scene,
  render paths, and feature flags.
- A visible image proves execution reached presentation; it does not by itself
  prove physically correct lighting, temporal stability, or optimal barriers.

## Captured v0.1 smoke evidence (2026-08-18)

- Hardware: NVIDIA GeForce RTX 5070 Ti, driver 595.95.
- Runtime: Visual Studio 2026 Release build, 1600x900 swapchain, Final Color
  display mode, hardware sRGB swapchain encoding.
- Scene: `assets/models/smoke_test/Box.gltf` with the benchmark camera and
  directional-light presets; exposure 1.0, ambient strength 0.0, light radius
  0.5.
- Feature flags: direct light, RT shadows, and IBL enabled; TAA and the SVGF
  master switch disabled. Ray Traced path alpha testing enabled.
- `forward.png` SHA-256:
  `BACD5950AEAA399E368DEB2199B47FD3554E1173CDA61CD0112E1C4B7C245FCF`
- `hybrid.png` SHA-256:
  `F7A8D3F1918E20F679E86A4C32A70747049CEB3EF998E02C5BF419941DC66A85`
- `ray-traced.png` SHA-256:
  `0DC19C3EED8843937BB1B1C95819058E8051625CDB25CE8EFE3876C27C7E15FC`

These Box captures are correctness smoke evidence, not showcase-quality
screenshots. Keep the original PNG files as local release artifacts and attach
them to the GitHub release if no stronger fixed-scene captures replace them.
