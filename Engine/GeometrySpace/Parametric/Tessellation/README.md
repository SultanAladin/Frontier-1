# 🧩 `Engine/GeometrySpace/Parametric/Tessellation/`

> **TL;DR**: Watertight body tessellation: one shared sampling per edge, constrained Delaunay refinement per face chart (4 source/header/shader files under `Engine/GeometrySpace/Parametric/Tessellation/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `BodyTessellation.cpp` | Watertight body tessellation: one shared sampling per edge, constrained Delaunay refinement per face chart |
| `BodyTessellation.h` | Watertight body tessellation: one shared sampling per edge, constrained Delaunay refinement per face chart |
| `ChartTriangulation.cpp` | Constrained Delaunay triangulation of a face chart: Bowyer–Watson insertion, Sloan constraint recovery, parity interior |
| `ChartTriangulation.h` | Constrained Delaunay triangulation of a face chart: Bowyer–Watson insertion, Sloan constraint recovery, parity interior |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometrySpace::Parametric`
- **Primary Types**: `TessellationSettings`, `PolygonNetEdge`, `PolygonNetFace`, `PolygonNetworkStructure`, `TessellationOutcome`, `ChartTriangle`, `ChartTriangulationStatistics`, `ChartTriangulation`

## Usage

```cpp
#include "Engine/GeometrySpace/Parametric/Tessellation/BodyTessellation.h"
#include "Engine/GeometrySpace/Parametric/Tessellation/ChartTriangulation.h"

using namespace Frontier::GeometrySpace::Parametric;
TessellationSettings Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
