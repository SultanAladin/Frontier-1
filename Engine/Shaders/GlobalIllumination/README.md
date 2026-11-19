# 🧩 `Engine/Shaders/GlobalIllumination/`

> **TL;DR**: Native Vulkan local distance baking, toroidal clipmap fill, conservative shadow queries and resident diffuse-cache gathering. (6 source/header/shader files under `Engine/Shaders/GlobalIllumination/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `DistanceEvaluation.slang` | Native Vulkan local distance baking, toroidal clipmap fill, conservative shadow queries and resident diffuse-cache gathering. |
| `DynamicIrradianceCache.slang` | GPU Dynamic Probe Radiance Cache: 64 Spherical Fibonacci BVH4 Rays -> 8x8 Octahedral Irradiance/pi + 16x16 Chebyshev Variance + Backface Gate |
| `SpecularGlassRaytrace.slang` | Tier 2 RTX Coherent VNDF Specular Reflection & Abbe-Dispersed Dielectric Glass Refraction Compute Pass |
| `VoxelSdfClipmapCompute.slang` | 2-Cascade Voxel SDF Clipmap + Dynamic Local Object SDF Union + Multi-Bounce Surface Cache + Penumbra Shadows + Deterministic Cone GI |
| `WeightedPathReplay.slang` | Modular ReSTIR Path Replay GI Pass: 4-Candidate Owen-Sobol RIS + 1-Bounce BVH Replay + DynamicProbe Multi-Bounce Tail |
| `WeightedTemporalFilter.slang` | Modular ReSTIR Temporal/Spatial Reservoir Reuse (with BVH Visibility Validation) + Variance-Guided Joint A-Trous SVGF Pass |

## Shader Entry Points & Compilation

| Shader | Entry Points | Compiled SPIR-V Outputs |
| :--- | :--- | :--- |
| `DistanceEvaluation.slang` | `BakeLocalDistance` (`compute`), `FillClipmap` (`compute`), `EvaluateDistanceProbes` (`compute`), `RelightVehicleCache` (`compute`), `GatherVehicleCache` (`compute`), `ClearVehicleCache` (`compute`) | `DistanceEvaluation.*.spv` |
| `DynamicIrradianceCache.slang` | `DynamicIrradianceCacheComputeEntry` (`compute`) | `DynamicIrradianceCache.*.spv` |
| `SpecularGlassRaytrace.slang` | `SpecularRaytraceComputeEntry` (`compute`) | `SpecularGlassRaytrace.*.spv` |
| `VoxelSdfClipmapCompute.slang` | `ComposeDynamicLocalSdfClip0Entry` (`compute`), `PropagateClipmapRadiosityBounceEntry` (`compute`), `EvaluateClipmapConeGiEntry` (`compute`) | `VoxelSdfClipmapCompute.*.spv` |
| `WeightedPathReplay.slang` | `WeightedPathReplayComputeEntry` (`compute`) | `WeightedPathReplay.*.spv` |
| `WeightedTemporalFilter.slang` | `WeightedTemporalFilterComputeEntry` (`compute`) | `WeightedTemporalFilter.*.spv` |

```cmake
# Shaders in this directory are compiled automatically at build time by CMakeLists.txt
# into ${CMAKE_BINARY_DIR}/CompiledShaders/ via ExternalPackages/VulkanCompute/Slang.
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
