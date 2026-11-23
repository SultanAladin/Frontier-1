# 🧩 `Engine/GeometrySpace/Selection/`

> **TL;DR**: Packed bitmask selection state across PolygonNetwork Vertices, DirectedEdges, and Faces with grow/shrink/invert operations (6 source/header/shader files under `Engine/GeometrySpace/Selection/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ComponentSelectionMask.cpp` | Implementation of ComponentSelectionMask bitset storage and neighbor expansion/contraction |
| `ComponentSelectionMask.h` | Packed bitmask selection state across PolygonNetwork Vertices, DirectedEdges, and Faces with grow/shrink/invert operations |
| `GeometrySelection.cpp` | Implementation of Edge Loop, Edge Ring, Dijkstra Geodesic Shortest Edge Path, Linked Island, Boundary, Sharp & Coplanar selection |
| `GeometrySelection.h` | Connectivity traversal selectors: Edge Loop, Edge Ring, Dijkstra Geodesic Shortest Path, Linked Island, Boundary, Sharp & Coplanar |
| `IslandFloodSelection.cpp` | Implementation of seam-bounded UV island flood segmentation, perimeter extraction, and UDIM tile tagging/reassignment |
| `IslandFloodSelection.h` | Seam-bounded UV island flood segmentation, perimeter loop extraction, and UDIM 1001-series tile selection/reassignment |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometrySpace`
- **Primary Types**: `ComponentSelectionMask`, `GeometrySelection`, `ExtractedUVIsland`

## Usage

```cpp
#include "Engine/GeometrySpace/Selection/ComponentSelectionMask.h"
#include "Engine/GeometrySpace/Selection/GeometrySelection.h"
#include "Engine/GeometrySpace/Selection/IslandFloodSelection.h"

using namespace Frontier::GeometrySpace;
ComponentSelectionMask Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
