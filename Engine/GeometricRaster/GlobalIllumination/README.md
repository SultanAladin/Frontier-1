# 🧩 `Engine/GeometricRaster/GlobalIllumination/`

> **TL;DR**: Three-Order World Rendering sequences — Order 1 Standalone Direct PBR (`VulkanWorldRenderSequence`), Order 2 Real-Time GI (`StokesContourIrradianceSequence` & `SurfelRadiosityLatticeSequence`), and Order 3 Unified Deterministic Path Tracer (`DeterministicPathTraceSequence` serving both `GameRealtime` and `SceneFinalRender`), routed through `TieredWorldRenderSequence`.

---

## Files

| File | Purpose |
| :--- | :--- |
| `DeterministicPathTraceSequence.cpp` | Vulkan 1.3 + Slang unified deterministic path tracer (`GameRealtime` & `SceneFinalRender` over one Owen-scrambled Sobol' `(0, 2)` OpenPBR integrator) |
| `DeterministicPathTraceSequence.h` | Host sequence for deterministic path tracing, `R32_UINT` visibility rasterization, temporal history reprojection, à-trous reconstruction, and IEEE-754 FNV-1a digest verification |
| `ShowcaseGalleryScene.cpp` | Cornell-Studio Showcase Gallery Scene with Outliner synchronization & interactive asset placement |
| `ShowcaseGalleryScene.h` | Cornell-Studio Showcase Gallery Scene with Outliner synchronization & interactive asset placement |
| `SpecularReflectionSequence.cpp` | BVH4 specular raytracing, analytical sky dome fallback, and chromatic dielectric refraction |
| `SpecularReflectionSequence.h` | BVH4 specular raytracing, analytical sky dome fallback, and chromatic dielectric refraction |
| `StokesContourIrradianceSequence.cpp` | Order 2 boundary-only Stokes polygonal contour irradiance with 1-bounce diffuse radiosity & `CompressedWideTree` / local-SDF visibility |
| `StokesContourIrradianceSequence.h` | Order 2 zero-bake Stokes contour diffuse GI sequence |
| `SurfelRadiosityLatticeSequence.cpp` | Order 2 2-cascade sparse spatial-hashed L1 spherical-harmonic surfel radiosity lattice (`16 B` cells) with Chebyshev visibility |
| `SurfelRadiosityLatticeSequence.h` | Order 2 sparse SH1 surfel radiosity lattice GI sequence |
| `TieredWorldRenderSequence.cpp` | Unified world render router across Order 1 (`MatCap`, `OpenPbrDirect`), Order 2 (`PbrStokesContourGi`, `PbrSurfelLatticeGi`), and Order 3 (`PathTracerGameRealtime`, `PathTracerSceneFinalRender`) |
| `TieredWorldRenderSequence.h` | World render sequence configuration, tier budgets (`GtxLowEnd` vs `HighEnd` `0 B` SDF), and telemetry |
| `VulkanWorldRenderSequence.cpp` | Order 1 Vulkan `.slang` GPU compute direct PBR sequence (`MatCap`, `OpenPbrDirect`, `.ies` photometry, sky LUT, `PlanetaryClouds`) |
| `VulkanWorldRenderSequence.h` | Order 1 Vulkan `.slang` GPU compute direct PBR sequence |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometricRaster`
- **Primary Types**: `DeterministicPathTraceSequence`, `DeterministicRenderProfile`, `DeterministicTracerConfiguration`, `StokesContourIrradianceSequence`, `SurfelRadiosityLatticeSequence`, `WorldRenderEngine`, `WorldRenderTier`, `TieredWorldRenderSequence`, `VulkanWorldRenderSequence`

## Usage

```cpp
#include "Engine/GeometricRaster/GlobalIllumination/DeterministicPathTraceSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"

Frontier::GeometricRaster::DeterministicPathTraceSequence PathTracer {};
```
