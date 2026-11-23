# 🧩 `Engine/Workspaces/UV/Solvers/`

> **TL;DR**: Priority-Layer-Weighted Concave Skyline Horizon Packer accelerated by ConcaveSkylineCompute.comp.spv & ExecutionChannels (14 source/header/shader files under `Engine/Workspaces/UV/Solvers/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ConcaveSkylinePacker.cpp` | Implementation of ConcaveSkylinePacker using ConcaveSkylineCompute.comp.spv + ExecutionChannels parallel island profiles |
| `ConcaveSkylinePacker.h` | Priority-Layer-Weighted Concave Skyline Horizon Packer accelerated by ConcaveSkylineCompute.comp.spv & ExecutionChannels |
| `ConformalUnwrapSolver.cpp` | Parallel island-by-island cotangent harmonic + PCA planar conformal UV parameterization with positive CCW orientation guarantee |
| `ConformalUnwrapSolver.h` | Parallel multi-lane Harmonic / Cotangent Conformal UV Flattener executing across Threading::ExecutionChannels |
| `ProgressiveStretchRelaxer.cpp` | Implementation of ProgressiveStretchRelaxer dispatching StretchRelaxCompute.comp.spv on Vulkan 1.2 Timeline Semaphores |
| `ProgressiveStretchRelaxer.h` | Vulkan .slang GPU Jacobi stretch-guided UV relaxation solver (StretchRelaxCompute.comp.spv) with zero-flip orientation guard |
| `SeamCutGenerator.cpp` | Implementation of dihedral sharp-angle seam stamping and connected UV island extraction on PolygonNetwork |
| `SeamCutGenerator.h` | Manifold dihedral sharp-angle seam cutter and connected seam-bounded UV island partitioner on PolygonNetwork |
| `TexelDensityEqualizer.cpp` | Implementation of physical texel density equalization across seam-bounded PolygonNetwork UV islands |
| `TexelDensityEqualizer.h` | Physical texel density equalizer (px/m) and multi-UDIM (1001..100N) tile distributor for PolygonNetwork |
| `UVIslandLayoutOperations.cpp` | Implementation of StackSimilarIslands, UnstackOverlappingIslands, and greedy non-overlapping ExpandIntoEmptyPockets |
| `UVIslandLayoutOperations.h` | Island layout operations: Stack Similar Islands, Unstack Overlapping Islands, and Greedy Fill-Pockets Expansion |
| `UVProjectionSolvers.cpp` | Implementation of ABF++, Smart UV Project, Box/Cylinder/Sphere/View projections, and Follow Active Quads strip solver |
| `UVProjectionSolvers.h` | Angle-Based Flattening (ABF++), Smart UV Angle-Cluster Projection, Analytic Box/Cylinder/Sphere/View & Follow Active Quads solvers |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::Workspaces`
- **Primary Types**: `GPUSkylineCandidateQuery`, `GPUSkylineScoreResult`, `GPUSkylineDispatchParams`, `RotatedCandidateProfile`, `PreparedPackIsland`, `ConcavePackReport`, `ConcaveSkylinePacker`, `SolvedIslandUVs`, `ConformalUnwrapReport`, `ConformalUnwrapSolver`, `GPURelaxCornerEntry`, `GPURelaxDispatchParams`, `StretchRelaxReport`, `ProgressiveStretchRelaxer`, `UVIslandPartition`, `SeamCutGenerator`, `TexelDensityReport`, `TexelDensityEqualizer` (+3 more)

## Usage

```cpp
#include "Engine/Workspaces/UV/Solvers/ConcaveSkylinePacker.h"
#include "Engine/Workspaces/UV/Solvers/ConformalUnwrapSolver.h"
#include "Engine/Workspaces/UV/Solvers/ProgressiveStretchRelaxer.h"

using namespace Frontier::Workspaces;
GPUSkylineCandidateQuery Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
