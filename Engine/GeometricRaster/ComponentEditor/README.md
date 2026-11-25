# 🧩 `Engine/GeometricRaster/ComponentEditor/`

> **TL;DR**: Procedural & compiled geometry vertex builders for Component Editor .slang pipelines (surface channels, constraints, shells, beams) (6 source/header/shader files under `Engine/GeometricRaster/ComponentEditor/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ComponentGizmoGeometry.cpp` | Implementation of procedural & compiled geometry vertex builders for Component Editor .slang pipelines |
| `ComponentGizmoGeometry.h` | Procedural & compiled geometry vertex builders for Component Editor .slang pipelines (surface channels, constraints, shells, beams) |
| `ComponentSpecimenBaker.cpp` | Implementation of offscreen transparent OpenPbrDirect studio thumbnail baker for ComponentClass & geometry assets |
| `ComponentSpecimenBaker.h` | Offscreen 128x128 transparent OpenPbrDirect studio thumbnail baker for ComponentClass & compiled geometry assets |
| `ComponentViewportRenderer.cpp` | Implementation of Vulkan 1.3 OpenPbrDirect (Zero GI) + 7-channel .slang inspection & Layer-1 X-ray constraint renderer |
| `ComponentViewportRenderer.h` | Vulkan 1.3 OpenPbrDirect (Zero GI) + 7-channel .slang inspection & Layer-1 X-ray constraint renderer for Component Editor |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometricRaster::ComponentEditor`
- **Primary Types**: `ComponentShaderVertex`, `GeometrySlotSlice`, `CompiledGeometryVisualBatch`, `ConstraintPieceCategory`, `ConstraintVisualPiece`, `ComponentGizmoGeometry`, `ComponentSpecimenBaker`, `ComponentSurfacePushConstantsGpu`, `Mat4`, `GpuVertexSliceUpload`, `CachedAssetGpuEntry`, `ComponentViewportRenderer`, `ComponentInspectionChannel`, `ComponentViewportCamera`, `ComponentRenderOptions`, `ComponentViewportOutput`

## Usage

```cpp
#include "Engine/GeometricRaster/ComponentEditor/ComponentGizmoGeometry.h"
#include "Engine/GeometricRaster/ComponentEditor/ComponentSpecimenBaker.h"
#include "Engine/GeometricRaster/ComponentEditor/ComponentViewportRenderer.h"

using namespace Frontier::GeometricRaster::ComponentEditor;
ComponentShaderVertex Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
