# 🧩 `Engine/SpatialAcceleration/`

> **TL;DR**: 128-triangle cluster LOD pyramid, 128 B 4-wide SAH BVH4 (`WideBoundingTree`), and 64 B 8-bit quantized 4-wide BVH4 with bottom-up refit (`CompressedWideTree`) under `Engine/SpatialAcceleration/`.

---

## Files

| File | Purpose |
| :--- | :--- |
| `ClusterPyramid.cpp` | 128-triangle Morton cluster partitioning and seam/border-locked QEM LOD simplification |
| `ClusterPyramid.h` | 128-triangle cluster LOD pyramid with seam/border-locked QEM simplification and normal-cone bounds |
| `CompressedWideTree.cpp` | 12-bin SAH builder, Ylitie-Karras-Laine 64 B 8-bit quantized 4-wide collapse, bottom-up dynamic refit, and rigid instancing |
| `CompressedWideTree.h` | 64-byte quantized 4-wide SAH BVH4 (`CompressedWideEntry64`) for Order 2 HighEnd 0 MB-SDF shadows and Order 3 deterministic path tracing |
| `TriangleVisibilitySpace.h` | Bounded double-precision triangle visibility queries over scene faces |
| `WideBoundingTree.cpp` | 12-bin SAH builder, 4-wide 128 B collapse, and deterministic closest-hit / shadow ray queries |
| `WideBoundingTree.h` | 128-byte 4-Wide SAH BVH4 acceleration tree for deterministic closest-hit and shadow occlusion queries |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::SpatialAcceleration`, `Frontier`
- **Primary Types**: `SymmetricQuadric10`, `ClusterTriangleEntry`, `ClusterCutSelection`, `ClusterPyramid`, `CompressedWideEntry64`, `RefittableBinaryEntry32`, `CompressedWideTree`, `RayTrianglePrimitive`, `RaySegmentQuery`, `RayIntersectionResult`, `TriangleSmoothNormals`, `WideBoundingTree`

## Usage

```cpp
#include "Engine/SpatialAcceleration/CompressedWideTree.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

using namespace Frontier::SpatialAcceleration;
CompressedWideTree Tree {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by `GtxSdfGiShadowValidation` and `DeterministicPathTracerValidation` under `Exhibits/`.
