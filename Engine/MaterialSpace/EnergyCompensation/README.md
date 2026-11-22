# 🧩 `Engine/MaterialSpace/EnergyCompensation/`

> **TL;DR**: 32x32 RG16F (4 KB) pre-integrated GGX directional & hemispherical albedo sheet for Turquin (2019) multi-scattering compensation (4 source/header/shader files under `Engine/MaterialSpace/EnergyCompensation/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `DirectionalAlbedoIntegral.cpp` | Implementation of deterministic VNDF QMC integration and bilinear Turquin multi-scattering lookup over the 4 KB RG16F sheet |
| `DirectionalAlbedoIntegral.h` | 32x32 RG16F (4 KB) pre-integrated GGX directional & hemispherical albedo sheet for Turquin (2019) multi-scattering compensation |
| `SheenCosineIntegral.cpp` | Implementation of Zeltner et al. (2022) Charlie/microfiber LTC fit and exact directional sheen albedo sheet (8 KB RGBA16F) |
| `SheenCosineIntegral.h` | 32x32 RGBA16F (8 KB) Zeltner, Burley & Chiang (SIGGRAPH 2022) Linearly Transformed Cosine (LTC) cloth sheen lookup table |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::MaterialSpace`
- **Primary Types**: `DirectionalAlbedoLookup`, `DirectionalAlbedoSheet`, `SheenLinearCosineCoefficients`, `SheenLinearCosineSheet`

## Usage

```cpp
#include "Engine/MaterialSpace/EnergyCompensation/DirectionalAlbedoIntegral.h"
#include "Engine/MaterialSpace/EnergyCompensation/SheenCosineIntegral.h"

using namespace Frontier::MaterialSpace;
DirectionalAlbedoLookup Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
