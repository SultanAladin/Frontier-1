# 🧩 `Engine/Shaders/GeometricRaster/`

> **TL;DR**: GPU compute optics unit, dual-engine world radiometric compute (`MatCap` & `OpenPbrDirect`), and surface/UV/gizmo rasterizers (14 `.slang` files under `Engine/Shaders/GeometricRaster/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `CameraVisualEffects.slang` | GPU compute optics unit evaluating Thin-Lens Depth of Field, Chromatic Aberration, Baked 3-Archetype Lens Flare, Vignette & Guides |
| `ConcaveSkylineCompute.slang` | Parallel GPU candidate placement scorer evaluating concave skyline horizon height + wasted pocket area across columns & rotations |
| `FractureSurfaceResolve.slang` | OpenPBR Surface + Studio Softbox IBL (No GI) + Energy/Cells/Normals debug modes for fracture shards in RHR +Z Up |
| `GizmoRasterization.slang` | Shared 3D & 2D Slate GizmoPRO handle rasterizer writing handle RGBA color + GizmoHandleOrdinal in PickIDs.w |
| `ImGuiOverlayRasterization.slang` | Native Vulkan .slang rasterizer rendering ImDrawData batches (Typography, Panes, ControlCentre, ContentBrowser & GPU Viewports) |
| `MatCapEvaluation.slang` | Shared baked engineering MatCap atlas sampling and Cook-Torrance studio-material evaluation |
| `SelectionMaskCompute.slang` | O(1) VRAM selection bitmask compute shader evaluating PriorityLayerOrdinal, PartGroupOrdinal, or Face selection operations in < 0.05 ms |
| `StretchRelaxCompute.slang` | Parallel Jacobi stretch-guided UV relaxation kernel preserving pinned vertices and seam boundaries on VRAM |
| `SurfaceShading.slang` | 3D PolygonNetwork surface rasterizer outputting HDR-tonemapped RGBA8 + simultaneous R32G32B32A32_UINT GPU Visibility Pick IDs |
| `UVDistortionInspection.slang` | Parallel GPU UV distortion, signed UV area, winding-flip & exact 2D SAT triangle-overlap evaluator feeding TimelineTransferRegister |
| `UVTileRasterization.slang` | 2D UDIM UVTile parameter canvas shader rendering checker/custom backdrops, authored-polygon UV edges, GPU seam/pin/selection & 2D Pick IDs |
| `ViewportPickReduction.slang` | GPU marquee-box & circular-brush reduction scanning R32G32B32A32_UINT pick buffer and atomically setting VRAM SelectionBitmask bits |
| `WorldViewportBlit.slang` | Fullscreen GPU viewport blit compositing the resolved world framebuffer in the swapchain render pass |
| `WorldViewportRadiometricCompute.slang` | Unified Dual-Engine Non-GI Compute Shader (`MatCap` & `OpenPbrDirect`, 5 Shadow Lab algorithms, BVH4/Sky reflections, Abbe glass, Atmosphere & `PlanetaryClouds` with 4x4 Cubic B-Spline upsample) |

## Shader Entry Points & Compilation

| Shader | Entry Points | Compiled SPIR-V Outputs |
| :--- | :--- | :--- |
| `CameraVisualEffects.slang` | `ComputeEntry` (`compute`) | `CameraVisualEffects.*.spv` |
| `ConcaveSkylineCompute.slang` | `ComputeEntry` (`compute`) | `ConcaveSkylineCompute.*.spv` |
| `FractureSurfaceResolve.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `GizmoRasterization.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `ImGuiOverlayRasterization.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `MatCapEvaluation.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `SelectionMaskCompute.slang` | `ComputeEntry` (`compute`) | `SelectionMaskCompute.*.spv` |
| `StretchRelaxCompute.slang` | `ComputeEntry` (`compute`) | `StretchRelaxCompute.*.spv` |
| `SurfaceShading.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `UVDistortionInspection.slang` | `ComputeEntry` (`compute`) | `UVDistortionInspection.*.spv` |
| `UVTileRasterization.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `ViewportPickReduction.slang` | `ComputeEntry` (`compute`) | `ViewportPickReduction.*.spv` |
| `WorldViewportBlit.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `WorldViewportRadiometricCompute.slang` | `CloudComputeEntry` (`compute`), `ComputeEntry` (`compute`), `DenoiseCompositeEntry` (`compute`), `ShadowCascadeComputeEntry` (`compute`) | `WorldViewportRadiometricCompute.*.spv` |

```cmake
# Shaders in this directory are compiled automatically at build time by CMakeLists.txt
# into ${CMAKE_BINARY_DIR}/CompiledShaders/ via ExternalPackages/VulkanCompute/Slang.
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
