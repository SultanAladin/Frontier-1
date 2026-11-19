# 🧩 `Engine/Shaders/Fluid/`

> **TL;DR**: MacCormack (BFECC + 6-neighbor min/max clamp) advection, Arrhenius combustion, cooling & soot generation (7 source/header/shader files under `Engine/Shaders/Fluid/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `FluidAdvectMacCormack.slang` | MacCormack (BFECC + 6-neighbor min/max clamp) advection, Arrhenius combustion, cooling & soot generation |
| `FluidDivergencePressure.slang` | Velocity divergence + gas expansion, Neumann Jacobi pressure + fire irradiance diffusion, and pressure gradient subtraction |
| `FluidLightReduction.slang` | Reduces 8 octants of hot fire cells into FluidEmissiveLightGpu proxies for OpenPBR, DDGI & PathTrace illumination |
| `FluidSolverLayouts.slang` | Shared +Z-Up Fluid GPU uniform blocks, 6-palette Blackbody evaluator, 3D curl noise, Dual-HG phase & ray-box intersection |
| `FluidSplatObstacle.slang` | Continuous +Z emitter, multi-lobe Rayleigh-Taylor blast detonation, particle splats & analytical/GI solid obstacle injection |
| `FluidVolumetricRaymarch.slang` | Depth-clipped dual-tier (Hero + World Drift) volumetric fire/smoke raymarcher & GI shadow cone transmittance |
| `FluidVorticityCombustion.slang` | 3D Curl vorticity confinement, +Z thermal buoyancy, baroclinic torque, curl turbulence & ForceFields advection |

## Shader Entry Points & Compilation

| Shader | Entry Points | Compiled SPIR-V Outputs |
| :--- | :--- | :--- |
| `FluidAdvectMacCormack.slang` | `FluidAdvectMacCormackEntry` (`compute`) | `FluidAdvectMacCormack.*.spv` |
| `FluidDivergencePressure.slang` | `FluidDivergenceEntry` (`compute`), `FluidJacobiPressureEntry` (`compute`), `FluidGradientSubtractEntry` (`compute`) | `FluidDivergencePressure.*.spv` |
| `FluidLightReduction.slang` | `FluidLightReductionEntry` (`compute`) | `FluidLightReduction.*.spv` |
| `FluidSolverLayouts.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `FluidSplatObstacle.slang` | `FluidSplatObstacleEntry` (`compute`) | `FluidSplatObstacle.*.spv` |
| `FluidVolumetricRaymarch.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `FluidVorticityCombustion.slang` | `FluidCurlComputeEntry` (`compute`), `FluidVorticityForcesEntry` (`compute`) | `FluidVorticityCombustion.*.spv` |

```cmake
# Shaders in this directory are compiled automatically at build time by CMakeLists.txt
# into ${CMAKE_BINARY_DIR}/CompiledShaders/ via ExternalPackages/VulkanCompute/Slang.
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
