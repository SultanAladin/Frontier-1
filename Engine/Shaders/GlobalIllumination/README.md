# 🧩 `Engine/Shaders/GlobalIllumination/`

> **TL;DR**: Tier 2 RTX coherent VNDF specular reflection & Abbe-dispersed dielectric glass refraction compute pass, plus the five-stage deterministic wavefront path tracer shared by GTX 4-wide BVH4 and RTX `VK_KHR_ray_query` (2 `.slang` shaders under `Engine/Shaders/GlobalIllumination/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `SpecularGlassRaytrace.slang` | Tier 2 RTX Coherent VNDF Specular Reflection & Abbe-Dispersed Dielectric Glass Refraction Compute Pass |
| `DeterministicWavefrontPathTrace.slang` | Five-stage deterministic wavefront path tracer (generate, closest, shadow, shade, resolve) with Owen Sobol (0,2) quadrature, GGX VNDF and 2^24 fixed-point sums; one source for GTX BVH4 and RTX inline ray query |

## Shader Entry Points & Compilation

| Shader | Entry Points | Compiled SPIR-V Outputs |
| :--- | :--- | :--- |
| `SpecularGlassRaytrace.slang` | `SpecularRaytraceComputeEntry` (`compute`) | `SpecularGlassRaytrace.*.spv` |
| `DeterministicWavefrontPathTrace.slang` | `WaveGeneratePrimary`, `WaveTraceClosest`, `WaveTraceShadow`, `WaveShadeScatter`, `WaveFinalizeSample` (`compute`) | `DeterministicWavefront*.comp.spv` (GTX `spirv_1_3`, RTX `spirv_1_5+spvRayQueryKHR` with `-DFRONTIER_RTX_RAY_QUERY`) |

```cmake
# Shaders in this directory are compiled automatically at build time by CMakeLists.txt
# into ${CMAKE_BINARY_DIR}/CompiledShaders/ via ExternalPackages/VulkanCompute/Slang.
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
