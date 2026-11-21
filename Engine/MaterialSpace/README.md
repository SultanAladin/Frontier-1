# 🧩 `Engine/MaterialSpace/`

> **TL;DR**: 128-byte cache-aligned bindless GPU material register and wave-uniform ActiveLobeMask table (Zero G-Buffer Bloat) (4 source/header/shader files under `Engine/MaterialSpace/`).

---

## Subdirectories

| Directory | Summary |
| :--- | :--- |
| [`Baking/`](Baking/README.md) | Calibrated studio shader-ball specimen rasterizer lighting any 10-lobe SurfaceMaterial for ContentBrowser & Exhibits |
| [`Dynamic/`](Dynamic/README.md) | Lowers a linear MaterialLayerStack into a flat MaterialInstructionSequence & Slang snippet (zero forced node authoring) |
| [`EnergyCompensation/`](EnergyCompensation/README.md) | 32x32 RG16F (4 KB) pre-integrated GGX directional & hemispherical albedo sheet for Turquin (2019) multi-scattering compensation |
| [`MaterialSurfaceLayer/`](MaterialSurfaceLayer/README.md) | Pre-integrated 32-byte secondary-bounce surface summary evaluated in 5 ALU operations during GI & multi-bounce raytracing |
| [`Scattering/`](Scattering/README.md) | Lobe 10B: Planckian spectral blackbody thermal emission radiance integrated into linear Rec.709 output |

## Files

| File | Purpose |
| :--- | :--- |
| `CompactMaterialRegister.cpp` | Implementation of IEEE-754 FP16 packing, 128-byte CompactMaterialEntry compilation, and wave-uniform lobe queries |
| `CompactMaterialRegister.h` | 128-byte cache-aligned bindless GPU material register and wave-uniform ActiveLobeMask table (Zero G-Buffer Bloat) |
| `SurfaceMaterial.cpp` | Implementation of physical domain sanitization, invariant validation, and 10-lobe reference material presets |
| `SurfaceMaterial.h` | Universal 10-lobe physical material specification unifying OpenPBR Surface 1.1 and constant-time multiscale glinty microflakes |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::MaterialSpace`
- **Primary Types**: `CompactMaterialRegister`, `MaterialPresetCategory`, `DiffuseSubstrateParameters`, `SpecularMetalParameters`, `ClearcoatLacquerParameters`, `ThinFilmWaveParameters`, `GlintMicroflakeParameters`, `DielectricTransmissionParameters`, `SubsurfaceDiffusionParameters`, `ClothSheenParameters`, `FiberStrandParameters`, `PorosityEmissionParameters`, `BindlessChannelBindings`, `SurfaceMaterial`

## Usage

```cpp
#include "Engine/MaterialSpace/CompactMaterialRegister.h"
#include "Engine/MaterialSpace/SurfaceMaterial.h"

using namespace Frontier::MaterialSpace;
CompactMaterialRegister Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
