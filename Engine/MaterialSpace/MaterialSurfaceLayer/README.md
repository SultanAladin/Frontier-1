# 🧩 `Engine/MaterialSpace/MaterialSurfaceLayer/`

> **TL;DR**: Pre-integrated 32-byte secondary-bounce surface summary evaluated in 5 ALU operations during GI & multi-bounce raytracing (4 source/header/shader files under `Engine/MaterialSpace/MaterialSurfaceLayer/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `BounceLayerSummary.cpp` | Implementation of pre-integrated 32 B BounceSlabEntry compilation and fast secondary-bounce shading |
| `BounceLayerSummary.h` | Pre-integrated 32-byte secondary-bounce surface summary evaluated in 5 ALU operations during GI & multi-bounce raytracing |
| `MaterialSurfaceLayer.cpp` | Implementation of vertical optical layer composition (coverage, relief height blend, dielectric coat over, moisture film) |
| `MaterialSurfaceLayer.h` | Ordered 8-layer physical surface stack (substrate, metallic flakes, pigment coat, dust/sheen, moisture film) |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::MaterialSpace`
- **Primary Types**: `MaterialLayerBlendCategory`, `MaterialSurfaceLayer`, `MaterialSurfaceStack`

## Usage

```cpp
#include "Engine/MaterialSpace/MaterialSurfaceLayer/BounceLayerSummary.h"
#include "Engine/MaterialSpace/MaterialSurfaceLayer/MaterialSurfaceLayer.h"

using namespace Frontier::MaterialSpace;
MaterialLayerBlendCategory Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
