# 🧩 `Engine/GeometrySpace/Primitives/`

> **TL;DR**: Parametric solid builders and 5 multi-part studio sample PolygonNetwork constructors with clean UDIM UVs and FacePartitionRegister (6 source/header/shader files under `Engine/GeometrySpace/Primitives/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `AnalyticPrimitives.cpp` | Implementation of parametric box, cylinder/frustum, torus ring, and 5 non-overlapping UDIM studio sample PolygonNetwork builders |
| `AnalyticPrimitives.h` | Parametric solid builders and 5 multi-part studio sample PolygonNetwork constructors with clean UDIM UVs and FacePartitionRegister |
| `RevolutionSweepPrimitives.cpp` | Implementation of lathe revolution, linear extrusion, and twist-minimized Bishop parallel-transport profile sweeps |
| `RevolutionSweepPrimitives.h` | Parametric lathe revolution, linear planar extrusion, and twist-minimized Bishop parallel-transport profile sweeps |
| `WatertightSewing.cpp` | Implementation of spatial-hashed vertex welding, outward winding unification, boundary loop closure, and solid volume audit |
| `WatertightSewing.h` | Spatial-hashed boundary vertex welding, consistent outward winding unification, planar hole closure, and solid volume audit |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometrySpace`
- **Primary Types**: `SampleAssetPreset`, `UVPatchBounds`, `ConstructedSampleAsset`, `LatheProfileWaypoint`, `SweepSpineWaypoint`, `QuantizedCellKey`, `QuantizedCellHasher`, `UndirectedEdgeTuple`, `WatertightAuditSummary`, `WatertightRepairSummary`

## Usage

```cpp
#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"
#include "Engine/GeometrySpace/Primitives/RevolutionSweepPrimitives.h"
#include "Engine/GeometrySpace/Primitives/WatertightSewing.h"

using namespace Frontier::GeometrySpace;
SampleAssetPreset Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
