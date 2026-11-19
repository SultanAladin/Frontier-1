# 🧩 `Engine/Shaders/MaterialSpace/`

> **TL;DR**: Fast 5-ALU 32-byte BounceSlabEntry evaluation for secondary IrradianceLattice & multi-bounce specular rays (12 source/header/shader files under `Engine/Shaders/MaterialSpace/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `BounceLayerSummary.slang` | Fast 5-ALU 32-byte BounceSlabEntry evaluation for secondary IrradianceLattice & multi-bounce specular rays |
| `DynamicMaskSeam.slang` | Exact 256-byte GPU uniform block matching C++20 DynamicParameterBlock + spatial sweep, impact pulse, lidar scan & voxel cutout masks |
| `DynamicMaterialResolve.slang` | GPU Direct Studio OpenPBR (No GI) compute shader resolving all 13 modular dynamic materials with 10-lobe physics & thin-film optics |
| `FiberScattering.slang` | Lobe 9: Chiang et al. (2016) / Marschner Dual-Scattering Hair & Technical Fiber BCSDF (R + TT + TRT lobes) |
| `GlintMicroflakeScattering.slang` | Lobe 5: Kemppinen et al. (SIGGRAPH Asia 2025) Constant-Time 4D Multiscale Glinty Microflake NDF (2 LOD x 4x4 O(1) evaluation) |
| `LinearlyTransformedSheen.slang` | Lobe 8: Zeltner, Burley & Chiang (SIGGRAPH 2022) Linearly Transformed Cosine (LTC) multiple-scattering cloth/velvet sheen |
| `OpenPbrSurfaceScattering.slang` | Master 10-Lobe OpenPBR 1.1 + Constant-Time Glinty BSDF unpacking 128 B CompactMaterialEntry with wave-uniform lobe branching |
| `ProceduralFieldGenerators.slang` | Modular GPU procedural field generators matching DynamicSurfaceResolver.cpp (Voronoi faults, Hex lattice, Gerstner ocean, rain beads) |
| `StudioEnvironmentRadiance.slang` | Calibrated GI-free studio prefiltered IBL with rectangular softbox panels & ACES filmic sRGB8 encoding for Direct Studio OpenPBR preview |
| `SubsurfaceTransmission.slang` | Lobe 6 & Lobe 7: Rough Microfacet GGX Dielectric Transmission + Abbe Dispersion + Christensen-Burley Subsurface Diffusion |
| `ThinFilmInterference.slang` | Lobe 4: Belcour & Barla (SIGGRAPH 2017) nanometer Airy multi-beam wave-optics thin-film interference |
| `TransmissiveSurfaceRasterization.slang` | Forward refractive & luminous surface overlay (Ocean Water refraction + Beer-Lambert depth, Hexagonal Energy Barrier, Smoked Rain Glass) |

## Shader Entry Points & Compilation

| Shader | Entry Points | Compiled SPIR-V Outputs |
| :--- | :--- | :--- |
| `BounceLayerSummary.slang` | `BounceSummaryComputeEntry` (`compute`) | `BounceLayerSummary.*.spv` |
| `DynamicMaskSeam.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `DynamicMaterialResolve.slang` | `DynamicStudioPreviewCompute` (`compute`) | `DynamicMaterialResolve.*.spv` |
| `FiberScattering.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `GlintMicroflakeScattering.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `LinearlyTransformedSheen.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `OpenPbrSurfaceScattering.slang` | `ComputeEntry` (`compute`) | `OpenPbrSurfaceScattering.*.spv` |
| `ProceduralFieldGenerators.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `StudioEnvironmentRadiance.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `SubsurfaceTransmission.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `ThinFilmInterference.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `TransmissiveSurfaceRasterization.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |

```cmake
# Shaders in this directory are compiled automatically at build time by CMakeLists.txt
# into ${CMAKE_BINARY_DIR}/CompiledShaders/ via ExternalPackages/VulkanCompute/Slang.
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
