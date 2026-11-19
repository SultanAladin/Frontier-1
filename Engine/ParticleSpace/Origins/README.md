# 🧩 `Engine/ParticleSpace/Origins/`

> **TL;DR**: Deterministic PRNG & analytic/structural release-origin samplers (Point, Sphere, Disc, Box, Lattice, Electrode, Web, Cloth, Flora) (9 source/header/shader files under `Engine/ParticleSpace/Origins/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `AnalyticOrigins.cpp` | Implementation of deterministic PCG PRNG, analytic release-origin samplers & structural anchor generators |
| `AnalyticOrigins.h` | Deterministic PRNG & analytic/structural release-origin samplers (Point, Sphere, Disc, Box, Lattice, Electrode, Web, Cloth, Flora) |
| `ControlVehicleSurfaceLoader.cpp` | Decodes Frontier's Controlled Integrator (ControlVehicle) FCT1 binary surfaces (Body + 4 Rims + 4 Tyres) and samples blue-noise anchors |
| `ControlVehicleSurfaceLoader.h` | Decodes Frontier's Controlled Integrator (ControlVehicle) FCT1 binary surfaces (Body + 4 Rims + 4 Tyres) and samples blue-noise anchors |
| `FireworkPatternSampler.cpp` | Implementation of all 10 pyrotechnic burst shapes & crisp 5x7 ASCII sky-writing bitmap sampler |
| `FireworkPatternSampler.h` | All 10 pyrotechnic shell & New Year's Sky-Text/Object pattern samplers (1:1 with FireworkPatterns.js) |
| `ReleaseOriginTypes.h` | Modular typed particle release-origin geometry descriptors (analytic, structural, 17 surface anchors, 10 firework patterns) |
| `SurfaceAnchorSampler.cpp` | Implementation of all 17 procedural surface anchor samplers (0..16) & area-weighted triangle network sampler |
| `SurfaceAnchorSampler.h` | All 17 procedural 3D surface presets (0..16) + custom triangle-network area-weighted sampler for ParticleSpace origins |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::ParticleSpace`
- **Primary Types**: `DeterministicPcgRng`, `OriginSpawnPoint`, `AxleMountSpec`, `DecodedFct1Stream`, `ControlVehiclePartCategory`, `ControlVehicleTriangle`, `ControlVehicleGeometrySet`, `Glyph5x7`, `ReleaseOriginCategory`, `SurfacePresetCategory`, `SurfaceSamplingStrategy`, `FireworkShapeCategory`, `AnalyticPointOrigin`, `AnalyticSphereOrigin`, `AnalyticDiscOrigin`, `AnalyticBoxOrigin`, `DualElectrodeOrigin`, `SurfaceLatticeOrigin` (+6 more)

## Usage

```cpp
#include "Engine/ParticleSpace/Origins/AnalyticOrigins.h"
#include "Engine/ParticleSpace/Origins/ControlVehicleSurfaceLoader.h"
#include "Engine/ParticleSpace/Origins/FireworkPatternSampler.h"

using namespace Frontier::ParticleSpace;
DeterministicPcgRng Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
