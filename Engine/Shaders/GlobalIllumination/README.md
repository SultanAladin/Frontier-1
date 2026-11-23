# 🧩 `Engine/Shaders/GlobalIllumination/`

> **TL;DR**: Order 2 Real-Time GI (`StokesContourIrradianceCompute`, `SurfelRadiosityLatticeCompute`), Specular Glass Raytrace, and Order 3 Unified Deterministic Path Tracer (`QuadratureSequence`, `VisibilityTriangleRasterization`, `DisplayReconstructionFilter`, `DisplayToneTransform`, `DeterministicRadianceIntegration`) — 8 `.slang` units compiling to 12 SPIR-V 1.3 binaries.

---

## Files

| File | Purpose |
| :--- | :--- |
| `DeterministicRadianceIntegration.slang` | Unified OpenPBR deterministic path tracer (`IntegrateRadiance`), temporal surface-validated history reprojection (`ReprojectHistory`), primary surface feature resolver (`ResolveSurfaceFeatures`), and device quadrature extractor (`ExtractQuadrature`) |
| `DisplayReconstructionFilter.slang` | Variance-estimating 3x3 history gather (`GatherEstimate`) + 4-step edge-stopping à-trous wavelet filter (`FilterAtrous`) fading to 0 by 64 spp |
| `DisplayToneTransform.slang` | Linear exposure (`2^EV`) + Stephen Hill RRT+ODT ACES fitted curve + IEC 61966-2-1 sRGB packing (`ComputeEntry`) |
| `QuadratureSequence.slang` | Shared Laine-Karras Owen-scrambled `(0, 2)` Sobol' generator + 12-bit void-and-cluster blue-noise digital XOR shift |
| `SpecularGlassRaytrace.slang` | Coherent VNDF specular reflection & Abbe-dispersed dielectric glass refraction compute shader |
| `StokesContourIrradianceCompute.slang` | Order 2 Stokes boundary contour irradiance + 8-ray cosine hemisphere visibility gate |
| `SurfelRadiosityLatticeCompute.slang` | Order 2 2-cascade sparse spatial-hashed L1 SH surfel radiosity lattice evaluation |
| `VisibilityTriangleRasterization.slang` | Subpixel-jittered `R32_UINT` primary visibility triangle rasterizer (`VertexEntry` + `FragmentEntry`) |

## Shader Entry Points & Compilation

| Shader | Entry Points | Compiled SPIR-V Outputs |
| :--- | :--- | :--- |
| `DeterministicRadianceIntegration.slang` | `IntegrateRadiance`, `ReprojectHistory`, `ResolveSurfaceFeatures`, `ExtractQuadrature` | `DeterministicRadianceIntegration.comp.spv`, `DeterministicHistoryReproject.comp.spv`, `DeterministicSurfaceFeatures.comp.spv`, `DeterministicQuadratureExtract.comp.spv` |
| `DisplayReconstructionFilter.slang` | `GatherEstimate`, `FilterAtrous` | `DisplayReconstructionGather.comp.spv`, `DisplayReconstructionAtrous.comp.spv` |
| `DisplayToneTransform.slang` | `ComputeEntry` | `DisplayToneTransform.comp.spv` |
| `SpecularGlassRaytrace.slang` | `SpecularRaytraceComputeEntry` | `SpecularGlassRaytrace.comp.spv` |
| `StokesContourIrradianceCompute.slang` | `ComputeEntry` | `StokesContourIrradianceCompute.comp.spv` |
| `SurfelRadiosityLatticeCompute.slang` | `ComputeEntry` | `SurfelRadiosityLatticeCompute.comp.spv` |
| `VisibilityTriangleRasterization.slang` | `VertexEntry`, `FragmentEntry` | `VisibilityTriangleRasterization.vert.spv`, `VisibilityTriangleRasterization.frag.spv` |
| `QuadratureSequence.slang` | Shared `.slang` include library | Included by `DeterministicRadianceIntegration.slang` & `VisibilityTriangleRasterization.slang` |

## Build & Verification

- Compiled automatically at build time by `CMakeLists.txt` (`FrontierShaders` target) into `${CMAKE_BINARY_DIR}/CompiledShaders/`.
