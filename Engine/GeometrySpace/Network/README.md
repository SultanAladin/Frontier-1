# 🧩 `Engine/GeometrySpace/Network/`

> **TL;DR**: PriorityAllocationLayer & UVRevision-stamped FacePartitionSpan enabling O(1) GPU layer selection and sub-0.05ms CPU spans (6 source/header/shader files under `Engine/GeometrySpace/Network/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `FacePartitionRegister.cpp` | Implementation of FacePartitionRegister single-pass O(F) reference indexing and zero-re-extraction layer selection queries |
| `FacePartitionRegister.h` | PriorityAllocationLayer & UVRevision-stamped FacePartitionSpan enabling O(1) GPU layer selection and sub-0.05ms CPU spans |
| `GeometryInstance.cpp` | Implementation of GeometryInstanceRegistry recycling indexed slots and maintaining non-kinship Enclosing/FirstNested/NextPeer links |
| `GeometryInstance.h` | Generation-authenticated GeometryInstance and contiguous GeometryInstanceRegistry with non-kinship index tree connectivity |
| `PolygonNetwork.cpp` | Implementation of PolygonNetwork loop construction, O(E log E) opposite-edge linking, normal evaluation, and .UVSpace stream exchange |
| `PolygonNetwork.h` | Half-edge PolygonNetwork storing contiguous Vertices, DirectedEdges, Faces, per-corner UVAxes, and per-face Material/MatCap assignments |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometrySpace`
- **Primary Types**: `PriorityAllocationLayer`, `FacePartitionSpan`, `FacePartitionRegister`, `InstanceAttributeMask`, `GeometryInstanceToken`, `GeometryInstance`, `GeometryInstanceRegistry`, `DirectedPairEntry`, `EdgeAttributeMask`, `FaceShadingAssignment`, `Vertex`, `DirectedEdge`, `Face`, `PolygonNetwork`

## Usage

```cpp
#include "Engine/GeometrySpace/Network/FacePartitionRegister.h"
#include "Engine/GeometrySpace/Network/GeometryInstance.h"
#include "Engine/GeometrySpace/Network/PolygonNetwork.h"

using namespace Frontier::GeometrySpace;
PriorityAllocationLayer Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
