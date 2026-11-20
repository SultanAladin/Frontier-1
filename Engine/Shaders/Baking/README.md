# 🧩 `Engine/Shaders/Baking/`

> **TL;DR**: Unquantized RGBA32F -> RGBA8 channel synthesis for all 17 surface targets (6 shader files under `Engine/Shaders/Baking/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `BakeChannelResolveCompute.slang` | Unquantized RGBA32F -> RGBA8 channel synthesis for all 17 surface targets: |
| `BakeDilationCompute.slang` | GPU UV edge gutter padding kernel extending covered texel borders smoothly outward across all 17 surface channel array layers. |
| `BakeLowUvRasterization.slang` | Full-GPU UV-space G-buffer rasterizer exporting unquantized RGBA32F LowPos/Mask, LowNormal/Part, LowTangent/Island, LowBitangent/Wire. |
| `BakeShaderLayouts.slang` | Shared GPU structures, mathematical helpers & Fibonacci hemisphere sampling for GTX BVH4 + RTX RayQuery surface baking. |
| `BakeSurfaceTransferCompute.slang` | Pure-GPU High-to-Low bidirectional cage raycaster + stratified Fibonacci AO / Bent Normal / Thickness kernel. |
| `BakeViewportPreview.slang` | Interactive 3D studio PBR + MatCap preview shader sampling live GPU-baked Tangent Normal, AO, Curvature & inspected channel overlays, |

## Shader Entry Points & Compilation

| Shader | Entry Points | Compiled SPIR-V Outputs |
| :--- | :--- | :--- |
| `BakeChannelResolveCompute.slang` | `ComputeEntry` (`compute`) | `BakeChannelResolveCompute.*.spv` |
| `BakeDilationCompute.slang` | `ComputeEntry` (`compute`) | `BakeDilationCompute.*.spv` |
| `BakeLowUvRasterization.slang` | `VertexEntry` / `FragmentEntry` | `BakeLowUvRasterization.*.spv` |
| `BakeShaderLayouts.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `BakeSurfaceTransferCompute.slang` | `ComputeEntry` (`compute`) | `BakeSurfaceTransferCompute.*.spv` |
| `BakeViewportPreview.slang` | `VertexEntry` / `FragmentEntry` | `BakeViewportPreview.*.spv` |

```cmake
# Shaders in this directory are compiled automatically at build time by CMakeLists.txt
# into ${CMAKE_BINARY_DIR}/CompiledShaders/ via ExternalPackages/VulkanCompute/Slang.
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
