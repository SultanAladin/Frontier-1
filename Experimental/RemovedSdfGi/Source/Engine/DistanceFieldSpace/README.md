# 🧩 `Engine/DistanceFieldSpace/`

> **TL;DR**: Sparse 8x8x8 narrow-band brick 3D Signed Distance Volume for Tier 1 GTX solar cone penumbra and 6-cone diffuse GI (6 source/header/shader files under `Engine/DistanceFieldSpace/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `SignedDistanceVolume.cpp` | Implementation of sparse 8x8x8 narrow-band brick 3D Signed Distance Volume, gradient sampling, and cone penumbra trace |
| `SignedDistanceVolume.h` | Sparse 8x8x8 narrow-band brick 3D Signed Distance Volume for Tier 1 GTX solar cone penumbra and 6-cone diffuse GI |
| `SobolDirections.h` | First 32 Joe–Kuo Sobol dimensions, generated using SciPy 1.15.3; see README for attribution. |
| `ToroidalDistanceClipmap.h` | Exact port of arena/f2d37e4b-frontier (Experimental/DistanceClipmap) algorithms: |
| `VoxelSdfClipmap.cpp` | Implementation of Unreal-Style 2-Cascade Voxel SDF Clipmap + Multi-Bounce Surface Cache + Dynamic Local Object Composition |
| `VoxelSdfClipmap.h` | Unreal-Style 2-Cascade Voxel SDF Clipmap + Multi-Bounce Surface Cache + Dynamic Local Object SDF Composition |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::DistanceFieldSpace`
- **Primary Types**: `SparseDistanceBrick`, `SignedDistanceVolume`, `ClipVector`, `RigidPose`, `SurfaceFace`, `Strike`, `FaceBvhRepository`, `ClosedBox`, `DistanceInterval`, `BakedFaceDistance`, `DistanceClipmap`, `ClosestTriHit`, `VoxelSdfClipmapConfiguration`, `DynamicClipmapUpdateStats`, `VoxelSurfaceCacheEntry`, `VoxelClipmapCascade`, `LocalGeometrySdfAsset`, `RigidTransform3D` (+3 more)

## Usage

```cpp
#include "Engine/DistanceFieldSpace/SignedDistanceVolume.h"
#include "Engine/DistanceFieldSpace/SobolDirections.h"
#include "Engine/DistanceFieldSpace/ToroidalDistanceClipmap.h"

using namespace Frontier::DistanceFieldSpace;
SparseDistanceBrick Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
