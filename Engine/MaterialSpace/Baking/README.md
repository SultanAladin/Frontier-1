# 🧩 `Engine/MaterialSpace/Baking/`

> **TL;DR**: Calibrated studio shader-ball specimen rasterizer lighting any 10-lobe SurfaceMaterial for ContentBrowser & Exhibits (4 source/header/shader files under `Engine/MaterialSpace/Baking/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `MaterialSpecimenRasterizer.cpp` | Implementation of calibrated studio shader-ball lighting (key sun + cool rim + QMC sky dome + refractive glass backdrop) |
| `MaterialSpecimenRasterizer.h` | Calibrated studio shader-ball specimen rasterizer lighting any 10-lobe SurfaceMaterial for ContentBrowser & Exhibits |
| `ProceduralChannelBaker.cpp` | Implementation of tileable procedural height synthesis, central-difference normals, & Toksvig mip roughness filtering |
| `ProceduralChannelBaker.h` | Deterministic tileable procedural normal, Toksvig-filtered roughness, & albedo mip-pyramid baker |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::MaterialSpace`
- **Primary Types**: `SpecimenPreviewImage`, `ProceduralPatternCategory`, `BakedChannelTexel`, `BakedChannelMipLevel`, `BakedChannelPyramid`

## Usage

```cpp
#include "Engine/MaterialSpace/Baking/MaterialSpecimenRasterizer.h"
#include "Engine/MaterialSpace/Baking/ProceduralChannelBaker.h"

using namespace Frontier::MaterialSpace;
SpecimenPreviewImage Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
