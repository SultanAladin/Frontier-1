# 🧩 `Engine/Shaders/`

> **TL;DR**: Subsystem umbrella grouping 12 child `.slang` shader modules under `Engine/Shaders/`.

---

## Subdirectories

| Directory | Summary |
| :--- | :--- |
| [`Atmosphere/`](Atmosphere/README.md) | Neckel & Labs (1994) Sun Disc, 6x Multi-Moon & Starfield, SkyView LUT, unified single-source `PlanetaryClouds` (`CloudNoise`/`CloudDensity`/`CloudTransport`/`CloudAerialPerspective`), Height/Aerial Fog, and Lens Flare |
| [`Baking/`](Baking/README.md) | Unquantized RGBA32F -> RGBA8 channel synthesis for all 17 surface targets |
| [`Colour/`](Colour/README.md) | Shared pure .slang GPU colour ramp LUT sampling, wrap/curve math, Oklab/LinearSrgb conversions, Kelvin blackbody & 3D lattice noise |
| [`ComponentEditor/`](ComponentEditor/README.md) | Compiled geometry shader supporting OpenPbrDirect (Zero GI) plus all 7 Component Editor inspection channels & MRT PickID |
| [`Compute/`](Compute/README.md) | Shared compute shader staging directory (reserved) |
| [`DirectLighting/`](DirectLighting/README.md) | Coplanar Plane-Aware Shadow Denoiser, Deterministic Vogel Solar Frustum Raytracer + Chassis AO, & Shared 4-Wide BVH4 Query Library |
| [`GeometricRaster/`](GeometricRaster/README.md) | Unified 2-Engine (`MatCap` & `OpenPbrDirect`) World Viewport Compute, Camera Visual Effects, Surface/UV/Gizmo Rasterization, & Pick Reduction |
| [`MaterialSpace/`](MaterialSpace/README.md) | Fast 5-ALU 32-byte BounceSlabEntry evaluation for secondary OpenPBR specular & refractive rays |
| [`Sketcher/`](Sketcher/README.md) | Shared CAD stroke expansion: homogeneous edge clipping, the six-corner pixel-width quad and its coverage ramp. |
| [`Vector/`](Vector/README.md) | Resolves 2x SSAA SurfacePreviewImage + StackedPick into FinalColorImage (RGBA8) & FinalPickImage (RGBA32UI), plus GPU vector guides/knots/pins/marquee. |
| [`Visibility/`](Visibility/README.md) | Zero-G-Buffer Analytic Ray-Differential Barycentric Resolver, UV Footprint Jacobians, Motion Vectors, & Wave-Uniform OpenPBR Shade |
| [`WorldRendering/`](WorldRendering/README.md) | Specular Reflection & Abbe-Dispersed Dielectric Glass Refraction Compute Unit |

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
