# 🧩 `Engine/MaterialSpace/Dynamic/`

> **TL;DR**: Lowers a linear MaterialLayerStack into a flat MaterialInstructionSequence & Slang snippet (zero forced node authoring) (11 source/header/shader files under `Engine/MaterialSpace/Dynamic/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `DynamicMaterialCompiler.cpp` | Implementation of MaterialLayerStack -> flat MaterialInstructionSequence & Slang code generation |
| `DynamicMaterialCompiler.h` | Lowers a linear MaterialLayerStack into a flat MaterialInstructionSequence & Slang snippet (zero forced node authoring) |
| `DynamicMaterialDefinitions.h` | Modular asset-agnostic enumerations and channel bitmasks for Frontier's layer-stack dynamic & procedural OpenPBR architecture |
| `DynamicMaterialRecipe.cpp` | Implementation of MaterialLayerStack operations, structural cache hashing & deterministic text serialization |
| `DynamicMaterialRecipe.h` | Authorable non-destructive MaterialLayerStack recipe (FinishLayer, ProceduralFieldLayer, MaskModifierLayer, SurfaceTransitionLayer) |
| `DynamicMaterialRepository.cpp` | Implementation of Frontier's 13 modular asset-agnostic dynamic OpenPBR material recipes |
| `DynamicMaterialRepository.h` | Curated repository of 13 modular, asset-agnostic dynamic & procedural OpenPBR material recipes |
| `DynamicParameterBlock.cpp` | Implementation of live 256-byte DynamicParameterBlock driver resolution, impact pulse queue & asset-agnostic bounds normalization |
| `DynamicParameterBlock.h` | Exact 256-byte GPU-aligned live uniform block updating dynamic material sliders, drivers, weather & 3D impact pulses at 120 Hz |
| `DynamicSurfaceResolver.cpp` | Implementation of calibrated CPU reference resolver with exact parity against Slang GPU procedural field & seam kernels |
| `DynamicSurfaceResolver.h` | Asset-agnostic CPU reference resolver converting any DynamicMaterialRecipe + DynamicParameterBlock into a full 10-lobe OpenPBR sample |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::MaterialSpace::Dynamic`
- **Primary Types**: `MaterialInstructionOp`, `CompiledMaterialSequence`, `MaterialLayerCategory`, `ProceduralFieldPattern`, `MaskModifierMode`, `ParameterDriverMode`, `BlendChannelBitmask`, `DynamicPresetIdentity`, `MaterialLayerEntry`, `NamedParameterSlot`, `MaterialLayerStack`, `DynamicMaterialRecipe`, `DynamicDriverInputs`, `VoronoiEdgeSample`, `HexCellSample`, `DynamicSurfaceQuery`, `ResolvedDynamicSurface`

## Usage

```cpp
#include "Engine/MaterialSpace/Dynamic/DynamicMaterialCompiler.h"
#include "Engine/MaterialSpace/Dynamic/DynamicMaterialDefinitions.h"
#include "Engine/MaterialSpace/Dynamic/DynamicMaterialRecipe.h"

using namespace Frontier::MaterialSpace::Dynamic;
MaterialInstructionOp Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
