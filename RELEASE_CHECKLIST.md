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

- [x] Push the branch and confirm `Windows CI` passes on GitHub. Run
      `32093464558` completed successfully for commit `75f384c`.
- [x] Repeat the documented VS2022 Release configure/build/test commands from
      a clean recursive checkout. GitHub Actions run `32093464558` completed
      the VS2022 Release build and all eight tests.
- [x] Review the README, changelog, license, and known limitations. The README
      rendered with its CI badge and public release-checklist link at commit
      `75f384c`; the release notes and limitations were reviewed during the
      `0.1.0` documentation freeze.
- [x] Capture one deterministic smoke baseline for each render path and record
      GPU, driver, resolution, scene, and feature flags.
- [x] Move the changelog entries from `Unreleased` to `0.1.0` with the release
      date (`2026-08-18`).
- [x] Create the `v0.1.0` tag and
      [GitHub release](https://github.com/CadenXc/HybridRenderer/releases/tag/v0.1.0);
      attach screenshots and the benchmark CSV rather than binaries with
      unverified runtime dependencies.

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

## Captured v0.1 benchmark evidence (2026-08-18)

- Artifact: `hybrid-benchmark.csv`.
- Hardware and workload: NVIDIA GeForce RTX 5070 Ti, Hybrid render path,
  1600x900, `smoke-test-box-v1` scene preset.
- Sampling: 120 warmup frames followed by 300 captured frames per pass.
- Render flags: `5891` (`Light`, `Shadow`, `SVGFTemporal`, `SVGFSpatial`,
  `IBL`, and `TAAHighQuality`; the SVGF and TAA master flags are disabled).
- Rows: six Render Graph passes, sorted by pass name.
- SHA-256:
  `4227B05E625D52A565C4009AC72D62999A44DCC3FC668C2CC49F21430965B499`.

The CSV contains per-pass GPU timestamp statistics. It does not measure CPU
frame time, queueing delay, presentation latency, or the sum of all work in a
frame. The Hybrid path currently retains its RT shadow, reflection, and
diffuse-GI dispatches whenever ray tracing and a TLAS are available, even when
their contribution flags are disabled; interpret the corresponding rows as the
cost of the current default graph rather than the cost of enabled effects only.
