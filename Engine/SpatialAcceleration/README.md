# 🧩 `Engine/SpatialAcceleration/`

> **TL;DR**: 128-triangle hierarchical cluster LOD pyramid with seam/border-locked QEM simplification and normal-cone bounds (5 source/header/shader files under `Engine/SpatialAcceleration/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ClusterPyramid.cpp` | Implementation of 128-triangle Morton cluster partitioning and seam/border-locked QEM hierarchical LOD simplification |
| `ClusterPyramid.h` | 128-triangle hierarchical cluster LOD pyramid with seam/border-locked QEM simplification and normal-cone bounds |
| `TriangleVisibilitySpace.h` | Bounded double-precision triangle visibility over all scene facets, |
| `WideBoundingTree.cpp` | Implementation of 12-bin SAH BVH builder, 4-wide 128 B collapse, and deterministic closest-hit / shadow ray queries |
| `WideBoundingTree.h` | Compressed 128-byte 4-Wide SAH BVH4 acceleration tree for deterministic GTX + RTX closest-hit and shadow occlusion queries |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::SpatialAcceleration`, `Frontier`
- **Primary Types**: `SymmetricQuadric10`, `ClusterTriangleEntry`, `ClusterCutSelection`, `ClusterPyramid`, `SpatialVector`, `TriangleVisibilitySpace`, `AxisAlignedBox`, `BinaryBuildSlot`, `BuildPrimitiveEntry`, `RayTrianglePrimitive`, `RaySegmentQuery`, `RayIntersectionResult`, `TriangleSmoothNormals`, `WideBoundingTree`

## Usage

```cpp
#include "Engine/SpatialAcceleration/ClusterPyramid.h"
#include "Engine/SpatialAcceleration/TriangleVisibilitySpace.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

using namespace Frontier::SpatialAcceleration;
SymmetricQuadric10 Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
