# 🧩 `Engine/GeometricRaster/Picking/`

> **TL;DR**: Unified 3D & 2D GPU R32G32B32A32_UINT visibility pick query supporting 1x1 cursor readback and marquee/brush VRAM reduction (2 source/header/shader files under `Engine/GeometricRaster/Picking/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ViewportPickQuery.cpp` | Implementation of ViewportPickQuery executing 1x1 GPU pixel readbacks and parallel ViewportPickReduction.slang region scans |
| `ViewportPickQuery.h` | Unified 3D & 2D GPU R32G32B32A32_UINT visibility pick query supporting 1x1 cursor readback and marquee/brush VRAM reduction |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometricRaster`
- **Primary Types**: `ViewportPickHit`, `RegionPickSummary`, `ViewportPickQuery`

## Usage

```cpp
#include "Engine/GeometricRaster/Picking/ViewportPickQuery.h"

using namespace Frontier::GeometricRaster;
ViewportPickHit Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
