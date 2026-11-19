# 🧩 `Engine/GeometricRaster/DirectLighting/`

> **TL;DR**: Visibility-Gated Clipped-Polygon Linearly Transformed Cosine (LTC) Quad Area Lights + 2-Scale (0.25 m / 2.2 m) Chassis & Specular AO (4 source/header/shader files under `Engine/GeometricRaster/DirectLighting/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `LinearCosineLightIntegration.cpp` | Visibility-Gated Clipped-Polygon Linearly Transformed Cosine (LTC) Quad Area Lights + 2-Scale (0.25 m / 2.2 m) Chassis & Specular AO |
| `LinearCosineLightIntegration.h` | Visibility-Gated Clipped-Polygon Linearly Transformed Cosine (LTC) Quad Area Lights + 2-Scale (0.25 m / 2.2 m) Chassis & Specular AO |
| `SolarShadowSequence.cpp` | Tiered Direct Solar Shadows (Zero VSM!): RTX Vogel Solar Frustum + Plane-Aware Filter & GTX 3.9 mm 2-Cascade Contact Clip + SDF Cone |
| `SolarShadowSequence.h` | Direct Solar Shadows (Zero SDF Shadows!): Shadow Lab Modes (ContactPCSS, PCSS+PlaneAware, SMRT, PCF, BVH4 Ray Query) |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometricRaster`, `Frontier::DistanceFieldSpace`
- **Primary Types**: `PolygonalAreaQuadLight`, `AmbientOcclusionSettings`, `DirectLightingOutput`, `LinearCosineLightIntegration`, `CascadeSampleOutcome`, `DirectShadowTier`, `SolarShadowAlgorithm`, `SolarDirectionalSource`, `OrthographicShadowCascade`, `SolarShadowFieldResult`, `SolarShadowSequence`

## Usage

```cpp
#include "Engine/GeometricRaster/DirectLighting/LinearCosineLightIntegration.h"
#include "Engine/GeometricRaster/DirectLighting/SolarShadowSequence.h"

using namespace Frontier::GeometricRaster;
PolygonalAreaQuadLight Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
