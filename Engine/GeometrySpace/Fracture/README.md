# 🧩 `Engine/GeometrySpace/Fracture/`

> **TL;DR**: Exact 3D convex polyhedron half-space clipping, PolygonNetwork bridges, 6-solver fracture dispatch, shared-cut bonds, and SDF section query (9 source/header/shader files under `Engine/GeometrySpace/Fracture/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ConvexPartitioning.cpp` | Exact 3D convex polyhedron half-space clipping, PolygonNetwork interchange, 6-solver fracture dispatch, bonds, and SDF sections |
| `ConvexPartitioning.h` | Exact 3D convex polyhedron half-space clipping, PolygonNetwork bridges, 6-solver fracture dispatch, shared-cut bonds, and SDF section query |
| `EngineeringBakedFracture.cpp` | Offline engineering fracture solver: Navier-Cauchy lattice, Rankine/Mohr-Coulomb wedge failure, wake relaxation, and non-planar fractography |
| `EngineeringBakedFracture.h` | Offline engineering fracture solver: 3D Navier-Cauchy stress lattice, Smith 3x3 eigensolver, stress-shadow wake, and non-planar fractography |
| `FastRuntimeFracture.cpp` | Sub-millisecond preallocated flat-array convex fracture kernel, Boussinesq-Hertz stress, and CutToken bond builder |
| `FastRuntimeFracture.h` | Sub-millisecond preallocated flat-array convex fracture solver with Boussinesq-Hertz stress and CutToken bond builder |
| `FractureMaterialRepository.cpp` | Physical SI fracture material repository and deterministic Mulberry32 PRNG stream (RHR +Z Up) |
| `FractureMaterialRepository.h` | Physical SI fracture material constants, solid primitives, solver methods, and deterministic PRNG stream |
| `FracturePartitionTypes.h` | Shared polyhedron, flat-cell, engineering surface, bond, and partition result structures (RHR +Z Up) |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometrySpace::Fracture`
- **Primary Types**: `VertexKey`, `VertexKeyHasher`, `GriffithCandidate`, `SolverOutput`, `PolyhedronClipOutput`, `PieceSdfSectionOutput`, `SdfVolumeFootprint`, `ConvexPartitioning`, `ElastostaticLattice`, `CutPlaneEntry`, `CrossingPlaneEquation`, `EngineeringSurfaceBatch`, `PrincipalStressEigen`, `EngineeringBakedFracture`, `FlatClipScratch`, `FastRuntimeFracture`, `FractureMaterialCategory`, `FractureSolidCategory` (+18 more)

## Usage

```cpp
#include "Engine/GeometrySpace/Fracture/ConvexPartitioning.h"
#include "Engine/GeometrySpace/Fracture/EngineeringBakedFracture.h"
#include "Engine/GeometrySpace/Fracture/FastRuntimeFracture.h"

using namespace Frontier::GeometrySpace::Fracture;
VertexKey Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
