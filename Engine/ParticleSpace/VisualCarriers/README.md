# 🧩 `Engine/ParticleSpace/VisualCarriers/`

> **TL;DR**: Modular visual render-geometry carrier types (all 7 carriers + all 20 procedural sprite cutouts + ColourSpace OpticalBlendEquation) (1 source/header/shader files under `Engine/ParticleSpace/VisualCarriers/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `VisualCarrierTypes.h` | Modular visual render-geometry carrier types (all 7 carriers + all 20 procedural sprite cutouts + ColourSpace OpticalBlendEquation) |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::ParticleSpace`
- **Primary Types**: `VisualCarrierCategory`, `SpriteCutoutCategory`, `BokehApertureCategory`, `BackingSurfaceStyle`, `SpriteBillboardCarrier`, `RibbonStrandCarrier`, `OrientedSolidCarrier`, `WingedCreatureCarrier`, `DielectricBeamCarrier`, `RefractionLensCarrier`, `BackingSurfaceCarrier`

## Usage

```cpp
#include "Engine/ParticleSpace/VisualCarriers/VisualCarrierTypes.h"

using namespace Frontier::ParticleSpace;
VisualCarrierCategory Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
