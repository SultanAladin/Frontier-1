# 🧩 `Engine/Shaders/VectorField/`

> **TL;DR**: Editor-only orthogonal XY/XZ/YZ cross-section scalar heatmap & analytical isolines rendered over Lit geometry (7 source/header/shader files under `Engine/Shaders/VectorField/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `CrossSectionHeatmapRasterization.slang` | Editor-only orthogonal XY/XZ/YZ cross-section scalar heatmap & analytical isolines rendered over Lit geometry |
| `ForcePathBeaconRasterization.slang` | Editor-only Lit-visible 3D motion paths (A->P->B curves/orbits), clickable Waypoint markers, Force circles & Anemometer cursor |
| `InfluenceEnvelopeRasterization.slang` | Editor-only shaded semi-transparent ForceModel reach envelopes & RadialBurst pulse shells (zero wireframes) |
| `LitBackdropRasterization.slang` | Editor Lit PBR architectural blockout & terrain surface rasterizer demonstrating Force Viz clarity over Lit shading |
| `StreamlineRasterization.slang` | Editor-only volumetric 3D GPU RK4 streamline ribbons with Cyan/Speed/Custom/Altitude modes, emissive boost & phase pulses |
| `VectorArrowRasterization.slang` | Editor-only GPU instanced 3D directional arrows colored by speed LUT & culled smoothly beneath quiet threshold |
| `VectorFieldRenderingShared.slang` | Shared Editor-only uniforms, Reverse-Z +Z Up projections, and PickID constants for VectorField .slang rasterization |

## Shader Entry Points & Compilation

| Shader | Entry Points | Compiled SPIR-V Outputs |
| :--- | :--- | :--- |
| `CrossSectionHeatmapRasterization.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `ForcePathBeaconRasterization.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `InfluenceEnvelopeRasterization.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `LitBackdropRasterization.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `StreamlineRasterization.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `VectorArrowRasterization.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `VectorFieldRenderingShared.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |

```cmake
# Shaders in this directory are compiled automatically at build time by CMakeLists.txt
# into ${CMAKE_BINARY_DIR}/CompiledShaders/ via ExternalPackages/VulkanCompute/Slang.
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
