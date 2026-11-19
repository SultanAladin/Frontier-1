# 🧩 `Engine/Shaders/Atmosphere/`

> **TL;DR**: Neckel & Labs (1994) Limb-Darkened Sun Disc + erfc PSF + K/F Corona + Radiant Glare + Sub-Pixel SolarErf, 6x Multi-Moon & Starfield (7 source/header/shader files under `Engine/Shaders/Atmosphere/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `CelestialDiscStarCompute.slang` | Neckel & Labs (1994) Limb-Darkened Sun Disc + erfc PSF + K/F Corona + Radiant Glare + Sub-Pixel SolarErf, 6x Multi-Moon & Starfield |
| `LensFlareBakeCompute.slang` | GPU Compute Baker generating the 512 x 768 RGBA8 Multi-Archetype Lens Flare Kernel Atlas (Anamorphic, Streaks, Halos) |
| `LensFlareCompute.slang` | Baked Multi-Archetype Lens Flare Sampling (Anamorphic, Streaks, Halos), Chromatic Halo Rings & Multi-Coated Aperture Ghosts |
| `SkyViewTransmittanceCompute.slang` | Stage 3 Physical Atmosphere: 256x64 Spherical-Horizon Transmittance + 32x32 Multi-Scatter + 192x108 Geometric-Limb SkyView LUTs |
| `StellarTileCullCompute.slang` | eos 16x16 Screen-Space Stellar Tile Culling + J2000 Catalog Projection (1640 BSC5 Stars -> 64-entry Tile Bins for Erf/Moffat PSF) |
| `VolumetricCloudCompute.slang` | Anti-Banded 64-Step IGN Raymarched 3D FBM Planetary Volumetric Clouds (12 Formations), Bounded LocalCloudVolume & 3-Octave Multi-Scattering |
| `VolumetricFogCompute.slang` | GPU Atmospheric Height Fog, Spectral Aerial Perspective & Wind-Advected Multi-Shape LocalFogVolumes |

## Shader Entry Points & Compilation

| Shader | Entry Points | Compiled SPIR-V Outputs |
| :--- | :--- | :--- |
| `CelestialDiscStarCompute.slang` | `ComputeEntry` (`compute`) | `CelestialDiscStarCompute.*.spv` |
| `LensFlareBakeCompute.slang` | `ComputeEntry` (`compute`) | `LensFlareBakeCompute.*.spv` |
| `LensFlareCompute.slang` | `ComputeEntry` (`compute`) | `LensFlareCompute.*.spv` |
| `SkyViewTransmittanceCompute.slang` | `TransmittanceLutComputeEntry` (`compute`), `MultiScatterLutComputeEntry` (`compute`), `ComputeEntry` (`compute`) | `SkyViewTransmittanceCompute.*.spv` |
| `StellarTileCullCompute.slang` | `ClearStellarTilesEntry` (`compute`), `ProjectCullStellarTilesEntry` (`compute`) | `StellarTileCullCompute.*.spv` |
| `VolumetricCloudCompute.slang` | `ComputeEntry` (`compute`) | `VolumetricCloudCompute.*.spv` |
| `VolumetricFogCompute.slang` | `ComputeEntry` (`compute`) | `VolumetricFogCompute.*.spv` |

```cmake
# Shaders in this directory are compiled automatically at build time by CMakeLists.txt
# into ${CMAKE_BINARY_DIR}/CompiledShaders/ via ExternalPackages/VulkanCompute/Slang.
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
