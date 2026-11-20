# 🧩 `Engine/Shaders/Visibility/`

> **TL;DR**: Zero-G-Buffer Analytic Ray-Differential Barycentric Resolver, UV Footprint Jacobians, Motion Vectors, & Wave-Uniform OpenPBR Shade (5 source/header/shader files under `Engine/Shaders/Visibility/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `AnalyticBarycentricResolve.slang` | Zero-G-Buffer Analytic Ray-Differential Barycentric Resolver, UV Footprint Jacobians, Motion Vectors, & Wave-Uniform OpenPBR Shade |
| `ClusterVisibilityCull.slang` | Two-Phase GPU Cluster Visibility Culler: Frustum + Normal-Cone + Continuous LOD Cut + Hi-Z Split (>=4 px HW vs <4 px SW) |
| `DepthPyramidDownsample.slang` | Conservative Reversed-Z Hi-Z mip chain downsampler with odd-dimension 3x3 floor guard (min reduction = farthest occluder horizon) |
| `HardwareVisibilityRasterization.slang` | >=4 px Indirect Hardware Visibility Rasterizer emitting exact 32-bit VisibilityToken32 + Reversed-Z hardware depth |
| `MicroFaceSoftwareRasterization.slang` | <4 px Sub-Pixel Compute Visibility Rasterizer packing (ReversedDepthBits32 << 32 \| VisibilityToken32) via 64-bit InterlockedMax |

## Shader Entry Points & Compilation

| Shader | Entry Points | Compiled SPIR-V Outputs |
| :--- | :--- | :--- |
| `AnalyticBarycentricResolve.slang` | `AnalyticResolveComputeEntry` (`compute`) | `AnalyticBarycentricResolve.*.spv` |
| `ClusterVisibilityCull.slang` | `ClusterCullComputeEntry` (`compute`) | `ClusterVisibilityCull.*.spv` |
| `DepthPyramidDownsample.slang` | `DepthPyramidComputeEntry` (`compute`) | `DepthPyramidDownsample.*.spv` |
| `HardwareVisibilityRasterization.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `MicroFaceSoftwareRasterization.slang` | `MicroFaceComputeEntry` (`compute`) | `MicroFaceSoftwareRasterization.*.spv` |

```cmake
# Shaders in this directory are compiled automatically at build time by CMakeLists.txt
# into ${CMAKE_BINARY_DIR}/CompiledShaders/ via ExternalPackages/VulkanCompute/Slang.
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
