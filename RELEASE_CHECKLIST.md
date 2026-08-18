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
- [ ] Repeat the documented VS2022 Release configure/build/test commands from
      a clean recursive clone.
- [ ] Review the README, changelog, license, and known limitations as they
      appear on GitHub.
- [ ] Capture one deterministic baseline for each render path and record GPU,
      driver, resolution, scene, and feature flags.
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
