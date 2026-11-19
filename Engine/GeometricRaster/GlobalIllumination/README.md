# 🧩 `Engine/GeometricRaster/GlobalIllumination/`

> **TL;DR**: Shared Bit-Exact Owen-Scrambled Sobol (0,2) Quadrature & Dupuy-Benyoub (2023) Spherical-Cap GGX VNDF (Zero RNG, Zero Denoiser) (15 source/header/shader files under `Engine/GeometricRaster/GlobalIllumination/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `DeterministicIntegrators.h` | Shared Bit-Exact Owen-Scrambled Sobol (0,2) Quadrature & Dupuy-Benyoub (2023) Spherical-Cap GGX VNDF (Zero RNG, Zero Denoiser) |
| `DynamicIrradianceCacheSequence.cpp` | GPU-Parity 8x8 Octahedral 1/pi Irradiance Probe Cache + 16x16 Chebyshev Radial Moments with Backface Validity Gating |
| `DynamicIrradianceCacheSequence.h` | GPU-Parity 8x8 Octahedral 1/pi Irradiance Probe Cache + 16x16 Chebyshev Radial Moments with Backface Validity Gating |
| `IrradianceLatticeSequence.cpp` | Dynamic Probe Cache Compatibility Facade (8x8 Octahedral + 16x16 Chebyshev Depth) + Mode-7 Debug Radiosity Volume |
| `IrradianceLatticeSequence.h` | Dynamic Probe Cache Compatibility Facade (8x8 Octahedral + 16x16 Chebyshev Depth) + Mode-7 Debug Radiosity Volume |
| `ShowcaseGalleryScene.cpp` | Canonical Architectural Cornell-Studio Gallery World Scene, EditorCamera, and OutlinerPanel bidirectional registration |
| `ShowcaseGalleryScene.h` | Canonical Architectural Cornell-Studio Gallery World Scene, EditorCamera, and OutlinerPanel bidirectional registration |
| `SpecularReflectionSequence.cpp` | Specular & Abbe Glass Refraction: BVH4 WideBoundingTree Raytrace or Analytical Sky Environment (Zero SDF Reflections!) |
| `SpecularReflectionSequence.h` | Specular & Abbe Glass Refraction: BVH4 WideBoundingTree Raytrace or Analytical Sky Environment (Zero SDF Reflections!) |
| `TieredWorldRenderSequence.cpp` | Unified 5-Engine World Render Sequence (MatCap, OpenPbrDirect, DdgiRaytracing, SdfGlobalIllumination, DeterministicPathTrace) |
| `TieredWorldRenderSequence.h` | Unified 5-Engine World Render Sequence (MatCap, OpenPbrDirect, DdgiRaytracing, SdfGlobalIllumination, DeterministicPathTrace) |
| `VulkanWorldRenderSequence.cpp` | Implementation of VulkanWorldRenderSequence executing WorldViewportRadiometricCompute.comp.spv on hardware Vulkan GPUs |
| `VulkanWorldRenderSequence.h` | Hardware Vulkan .slang compute sequence executing all 5 unified World Render Engines on the GPU |
| `WeightedPathReplaySequence.cpp` | Hybrid ReSTIR Path Replay GI: Candidate RIS, Temporal/Spatial Reservoir Reuse, DynamicProbe 2nd-Bounce & 5-Pass Joint A-Trous SVGF |
| `WeightedPathReplaySequence.h` | Hybrid ReSTIR Path Replay GI: Candidate RIS, Temporal/Spatial Reservoir Reuse, DynamicProbe 2nd-Bounce & 5-Pass Joint A-Trous SVGF |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometricRaster`
- **Primary Types**: `DynamicIrradianceCell`, `DynamicIrradianceCacheConfiguration`, `DynamicIrradianceCacheSequence`, `OctahedralIrradianceCell`, `IrradianceLatticeConfiguration`, `SignedDistanceRadiosityVolume`, `IrradianceLatticeSequence`, `LocalQuantizedKey`, `LocalCornerTriEntry`, `ShowcasePrimitiveShape`, `ShowcaseGeometryAsset`, `CachedLocalSmoothTriangle`, `ShowcaseWorldAssetEntry`, `ShowcaseAreaLightLink`, `ShowcaseWorldSceneBundle`, `SpecularReflectionSource`, `SpecularReflectionBuffer`, `SpecularReflectionSequence` (+32 more)

## Usage

```cpp
#include "Engine/GeometricRaster/GlobalIllumination/DeterministicIntegrators.h"
#include "Engine/GeometricRaster/GlobalIllumination/DynamicIrradianceCacheSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/IrradianceLatticeSequence.h"

using namespace Frontier::GeometricRaster;
DynamicIrradianceCell Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
