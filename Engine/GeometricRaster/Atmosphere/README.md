# 🧩 `Engine/GeometricRaster/Atmosphere/`

> **TL;DR**: Physical Bruneton/Hillaire Rayleigh + Mie + Ozone atmospheric transmittance, SkyView LUT, solar disc/corona, unified single-source Slang `PlanetaryClouds`, multi-moon/starfield, wind-coupled height/aerial fog, and lens flare (16 files under `Engine/GeometricRaster/Atmosphere/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `AtmosphericSkySequence.cpp` | Physical Bruneton/Hillaire Rayleigh + Mie + Ozone atmospheric transmittance, SkyView LUT, outer-space planetary limb & Eddington solar disc |
| `AtmosphericSkySequence.h` | Physical Bruneton/Hillaire Rayleigh + Mie + Ozone atmospheric transmittance, SkyView LUT, and solar disc/corona radiance |
| `AtmosphericWindSolver.cpp` | Implementation of AtmosphericWindSolver coupling Global/Cloud wind, Ekman veer, gust harmonics, and convective turbulence |
| `AtmosphericWindSolver.h` | Unified atmospheric wind solver bridging authored Wind parameters to PlanetaryClouds & VolumetricFog |
| `CelestialEphemerisSolver.cpp` | Astronomical solar ephemeris solver, Kelvin Planckian tint conversion, and Right-Handed (+Z Up) sky direction mapping |
| `CelestialEphemerisSolver.h` | Astronomical solar ephemeris solver, Kelvin Planckian tint conversion, and Right-Handed (+Z Up) sky direction mapping |
| `CloudNoiseVolume.h` | Precomputed seamless 3D condensate Worley/Perlin/blue-noise LUT (`CondensateNoise::QueryTexels()`) shared by CPU (`PlanetaryCloudGenerated.hpp`) and GPU (`PlanetaryCloudNoise`) |
| `J2000BrightStarRepository.h` | Yale Bright Star Catalogue (BSC5, Hoffleit & Warren 1991): 1640 J2000 navigation stars (V <= 5.0 + M45 Pleiades + M42 Trapezium) |
| `LensFlareSequence.cpp` | Baked multi-archetype Lens Flare optical kernel atlas (Anamorphic, Streaks, Halos), GPU bake dispatch & CPU/GPU evaluation |
| `LensFlareSequence.h` | Baked multi-archetype Lens Flare optical kernel atlas (Anamorphic, Streaks, Halos), GPU bake dispatch & CPU/GPU evaluation |
| `LunarStarfieldSequence.cpp` | Multi-instance Lunar discs (6 real 2K atlas presets, Lommel-Seeliger regolith, off-limb exospheres) & pinpoint blackbody Starfield |
| `LunarStarfieldSequence.h` | Multi-instance Lunar discs (6 real 2K surface presets, Lommel-Seeliger regolith, off-limb exospheres) & pinpoint blackbody Starfield |
| `VolumetricCloudSequence.cpp` | Single-source Slang `PlanetaryClouds` CPU execution (`PlanetaryCloudGenerated.hpp` + `CloudNoiseVolume.h`) across 12 formations, `BoundedCloudVolume`, and solar cloud shadows |
| `VolumetricCloudSequence.h` | Single-source multi-scattering planetary volumetric cloud deck (12 formations), BoundedCloudVolume, and solar cloud shadows |
| `VolumetricFogSequence.cpp` | Analytical Height Fog, Spectral Aerial Perspective, and Wind-Advected Multi-Shape LocalFogVolume raymarcher |
| `VolumetricFogSequence.h` | Atmospheric Height Fog, Spectral Aerial Perspective, and Wind-Driven Multi-Shape LocalFogVolume raymarching |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometricRaster`, `Frontier::GeometricRaster::CondensateNoise`
- **Primary Types**: `MediumCoefficientsKm`, `AtmosphericMediumConfiguration`, `AtmosphericSkySequence`, `WindCouplingSource`, `AtmosphericVolumeShape`, `AtmosphericWindParameters`, `ResolvedWindSample`, `AtmosphericWindSolver`, `SolarTintSource`, `SolarAppearanceMode`, `DayCycleSpeedPreset`, `SolarCelestialConfiguration`, `SolarEphemerisOutcome`, `CelestialEphemerisSolver`, `J2000RepositoryStarSpec`, `ProjectedLensFlareSlice`, `LensFlareSequence`, `LunarExosphereParams` (+24 more)

## Usage

```cpp
#include "Engine/GeometricRaster/Atmosphere/AtmosphericSkySequence.h"
#include "Engine/GeometricRaster/Atmosphere/AtmosphericWindSolver.h"
#include "Engine/GeometricRaster/Atmosphere/CelestialEphemerisSolver.h"

using namespace Frontier::GeometricRaster;
MediumCoefficientsKm Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
