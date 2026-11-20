# 🧩 `Engine/GeometricRaster/DirectLighting/`

> **TL;DR**: Clipped-polygon LTC area lights, tiered solar shadows, GTX hybrid local-SDF + capsule cone shadows (`HybridConeShadowSequence`), and Conty Estevez & Kulla `.ies` importance light trees (`PhotometricLightTree`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `HybridConeShadowSequence.cpp` | Tiered soft penumbra shadow solver: `GtxLowEnd` local `C0` SDF + `ArticulatedCapsuleRig` cone trace vs `HighEnd` 0 MB-SDF `CompressedWideTree` Vogel ray trace |
| `HybridConeShadowSequence.h` | Hybrid cone-traced / 4-wide quantized BVH4 soft-shadow sequence enforcing `0 B` SDF memory on `HighEnd` |
| `LinearCosineLightIntegration.cpp` | Visibility-gated clipped-polygon Linearly Transformed Cosine (LTC) area lights, `.ies` practical photometry, & 2-scale chassis AO |
| `LinearCosineLightIntegration.h` | Visibility-gated clipped-polygon LTC area lights, `.ies` practical photometry, & 2-scale chassis AO |
| `PhotometricLightTree.cpp` | 5x10 studio parking bay solver, 228-luminaire `.ies` placement, Conty Estevez & Kulla binary light tree, and unified GPU light register packer |
| `PhotometricLightTree.h` | Binary photometric `.ies` importance light tree (`LightClusterEntry`) and adaptive per-shading-point subtree cut (`AdaptiveLampCutCeiling = 8`) |
| `SolarShadowSequence.cpp` | Tiered direct solar shadows: Vogel solar frustum + plane-aware filter & 3.9 mm 2-cascade contact clip |
| `SolarShadowSequence.h` | Direct solar shadow modes (ContactPCSS, PCSS+PlaneAware, SMRT, PCF, BVH4 Ray Query) |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometricRaster`
- **Primary Types**: `HybridConeShadowSequence`, `PhotometricLightTree`, `VehicleBayPlacement`, `PracticalLampPlacement`, `LightClusterEntry`, `PackedLightRegisterLayout`, `DirectPointLightSpec`, `PolygonalAreaQuadLight`, `LinearCosineLightIntegration`, `SolarShadowSequence`

## Usage

```cpp
#include "Engine/GeometricRaster/DirectLighting/HybridConeShadowSequence.h"
#include "Engine/GeometricRaster/DirectLighting/PhotometricLightTree.h"

using namespace Frontier::GeometricRaster;
PackedLightRegisterLayout Layout {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by `DirectPbrWorldValidation`, `GtxSdfGiShadowValidation`, and `DeterministicPathTracerValidation`.
